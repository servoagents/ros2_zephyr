// SPDX-License-Identifier: Apache-2.0

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cyclonedds_c_test_msgs/msg/nested_fixed.h>
#include <dds/ddsrt/heap.h>
#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

typedef struct allocation_header_s {
  size_t size;
} allocation_header_t;

typedef struct allocation_metrics_s {
  size_t calls;
  size_t frees;
  size_t live_bytes;
  size_t high_water_bytes;
} allocation_metrics_t;

static allocation_metrics_t ros_metrics;
static allocation_metrics_t dds_metrics;
static volatile bool received;
static cyclonedds_c_test_msgs__msg__NestedFixed received_message;

struct thread_metrics {
  size_t count;
  size_t reserved_bytes;
};

static void report_thread(const struct k_thread *thread, void *user_data)
{
  struct thread_metrics *metrics = user_data;
  size_t unused = 0U;
  const int stack_result = k_thread_stack_space_get(thread, &unused);
  const char *name = k_thread_name_get((k_tid_t)thread);

  metrics->count++;
  metrics->reserved_bytes += thread->stack_info.size;
  printf("ROS2_ZEPHYR_STACK name=%s priority=%d reserved=%zu unused=%s",
         name != NULL && name[0] != '\0' ? name : "unnamed", k_thread_priority_get((k_tid_t)thread),
         thread->stack_info.size, stack_result == 0 ? "measured" : "unavailable");
  if (stack_result == 0) {
    printf(" unused_bytes=%zu", unused);
  }
  printf("\n");
}

static void report_threads(void)
{
  struct thread_metrics metrics = {0};
  k_thread_foreach(report_thread, &metrics);
  printf("ROS2_ZEPHYR_STACK_TOTAL threads=%zu reserved=%zu\n", metrics.count,
         metrics.reserved_bytes);
}

static void metrics_add(allocation_metrics_t *metrics, size_t size)
{
  metrics->calls++;
  metrics->live_bytes += size;
  if (metrics->live_bytes > metrics->high_water_bytes) {
    metrics->high_water_bytes = metrics->live_bytes;
  }
}

static void *tracked_allocate(size_t size, void *state)
{
  allocation_metrics_t *metrics = state;
  allocation_header_t *header = malloc(sizeof(*header) + size);
  if (header == NULL) {
    return NULL;
  }
  header->size = size;
  metrics_add(metrics, size);
  return header + 1;
}

static void tracked_deallocate(void *pointer, void *state)
{
  if (pointer == NULL) {
    return;
  }
  allocation_metrics_t *metrics = state;
  allocation_header_t *header = (allocation_header_t *)pointer - 1;
  metrics->frees++;
  metrics->live_bytes -= header->size;
  free(header);
}

static void *tracked_reallocate(void *pointer, size_t size, void *state)
{
  if (pointer == NULL) {
    return tracked_allocate(size, state);
  }
  allocation_metrics_t *metrics = state;
  allocation_header_t *old_header = (allocation_header_t *)pointer - 1;
  const size_t old_size = old_header->size;
  allocation_header_t *new_header = realloc(old_header, sizeof(*new_header) + size);
  if (new_header == NULL) {
    return NULL;
  }
  metrics->calls++;
  metrics->live_bytes -= old_size;
  metrics->live_bytes += size;
  if (metrics->live_bytes > metrics->high_water_bytes) {
    metrics->high_water_bytes = metrics->live_bytes;
  }
  new_header->size = size;
  return new_header + 1;
}

static void *tracked_zero_allocate(size_t count, size_t size, void *state)
{
  if (size != 0U && count > SIZE_MAX / size) {
    return NULL;
  }
  const size_t bytes = count * size;
  void *pointer = tracked_allocate(bytes, state);
  if (pointer != NULL) {
    memset(pointer, 0, bytes);
  }
  return pointer;
}

static void *dds_allocate(size_t size) { return tracked_allocate(size, &dds_metrics); }

static void *dds_zero_allocate(size_t count, size_t size)
{
  return tracked_zero_allocate(count, size, &dds_metrics);
}

static void *dds_reallocate(void *pointer, size_t size)
{
  return tracked_reallocate(pointer, size, &dds_metrics);
}

static void dds_deallocate(void *pointer) { tracked_deallocate(pointer, &dds_metrics); }

static void subscription_callback(const void *message)
{
  received_message = *(const cyclonedds_c_test_msgs__msg__NestedFixed *)message;
  received = true;
}

static bool check(rcl_ret_t result, const char *operation)
{
  if (result == RCL_RET_OK) {
    return true;
  }
  printf("ROS2_ZEPHYR_ERROR operation=%s code=%d detail=%s\n", operation, (int)result,
         rcl_get_error_string().str);
  rcl_reset_error();
  return false;
}

int main(void)
{
  printf("ROS2_ZEPHYR_START board=%s path=rclc-rcl-rmw_cyclonedds_c-cyclonedds\n",
         CONFIG_BOARD_TARGET);

  const ddsrt_allocation_ops_t dds_allocator = {
      .malloc = dds_allocate,
      .calloc = dds_zero_allocate,
      .realloc = dds_reallocate,
      .free = dds_deallocate,
  };
  ddsrt_set_allocator(dds_allocator);

  rcl_allocator_t allocator = {
      .allocate = tracked_allocate,
      .deallocate = tracked_deallocate,
      .reallocate = tracked_reallocate,
      .zero_allocate = tracked_zero_allocate,
      .state = &ros_metrics,
  };
  rclc_support_t support = {0};
  rcl_node_t node = rcl_get_zero_initialized_node();
  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
  cyclonedds_c_test_msgs__msg__NestedFixed outgoing = {0};
  cyclonedds_c_test_msgs__msg__NestedFixed incoming = {0};
  bool support_initialized = false;
  bool node_initialized = false;
  bool publisher_initialized = false;
  bool subscription_initialized = false;
  bool executor_initialized = false;
  int status = 1;

  if (!check(rclc_support_init(&support, 0, NULL, &allocator), "support_init")) {
    goto cleanup;
  }
  support_initialized = true;
  if (!check(rclc_node_init_default(&node, "ros2_zephyr_loopback", "", &support), "node_init")) {
    goto cleanup;
  }
  node_initialized = true;
  if (!check(rclc_publisher_init_best_effort(
                 &publisher, &node,
                 ROSIDL_GET_MSG_TYPE_SUPPORT(cyclonedds_c_test_msgs, msg, NestedFixed),
                 "ros2_zephyr_loopback"),
             "publisher_init")) {
    goto cleanup;
  }
  publisher_initialized = true;
  if (!check(rclc_subscription_init_best_effort(
                 &subscription, &node,
                 ROSIDL_GET_MSG_TYPE_SUPPORT(cyclonedds_c_test_msgs, msg, NestedFixed),
                 "ros2_zephyr_loopback"),
             "subscription_init")) {
    goto cleanup;
  }
  subscription_initialized = true;
  if (!check(rclc_executor_init(&executor, &support.context, 1U, &allocator), "executor_init")) {
    goto cleanup;
  }
  executor_initialized = true;
  if (!check(rclc_executor_add_subscription(&executor, &subscription, &incoming,
                                            subscription_callback, ON_NEW_DATA),
             "executor_add_subscription")) {
    goto cleanup;
  }

  const size_t ros_setup_calls = ros_metrics.calls;
  const size_t dds_setup_calls = dds_metrics.calls;
  outgoing.counter.data = UINT32_C(42424242);
  outgoing.samples.values[0] = 10U;
  outgoing.samples.values[1] = 11U;
  outgoing.samples.values[2] = 12U;
  outgoing.samples.values[3] = 13U;

  for (unsigned int attempt = 0U; attempt < 100U && !received; ++attempt) {
    if (!check(rcl_publish(&publisher, &outgoing, NULL), "publish") ||
        !check(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(20)), "spin_some")) {
      goto cleanup;
    }
    k_sleep(K_MSEC(10));
  }

  if (!received || received_message.counter.data != outgoing.counter.data ||
      memcmp(received_message.samples.values, outgoing.samples.values,
             sizeof(outgoing.samples.values)) != 0) {
    printf("ROS2_ZEPHYR_ERROR operation=loopback received=%d value=%" PRIu32 "\n", received,
           received_message.counter.data);
    goto cleanup;
  }

  printf("ROS2_ZEPHYR_LOOPBACK_PASS value=%" PRIu32 " array=%" PRIu32 ",%" PRIu32 ",%" PRIu32
         ",%" PRIu32 "\n",
         received_message.counter.data, received_message.samples.values[0],
         received_message.samples.values[1], received_message.samples.values[2],
         received_message.samples.values[3]);
  printf("ROS2_ZEPHYR_ALLOC setup_ros_calls=%zu steady_ros_calls=%zu"
         " setup_dds_calls=%zu steady_dds_calls=%zu"
         " ros_high_water=%zu dds_high_water=%zu\n",
         ros_setup_calls, ros_metrics.calls - ros_setup_calls, dds_setup_calls,
         dds_metrics.calls - dds_setup_calls, ros_metrics.high_water_bytes,
         dds_metrics.high_water_bytes);
  report_threads();
  status = 0;

cleanup:
  if (executor_initialized && rclc_executor_fini(&executor) != RCL_RET_OK) {
    status = 1;
  }
  if (subscription_initialized && rcl_subscription_fini(&subscription, &node) != RCL_RET_OK) {
    status = 1;
  }
  if (publisher_initialized && rcl_publisher_fini(&publisher, &node) != RCL_RET_OK) {
    status = 1;
  }
  if (node_initialized && rcl_node_fini(&node) != RCL_RET_OK) {
    status = 1;
  }
  if (support_initialized && rclc_support_fini(&support) != RCL_RET_OK) {
    status = 1;
  }
  printf("ROS2_ZEPHYR_CLEANUP status=%d ros_live=%zu dds_live=%zu\n", status,
         ros_metrics.live_bytes, dds_metrics.live_bytes);
#ifdef CONFIG_ARCH_POSIX
  posix_exit(status);
#endif
  return status;
}
