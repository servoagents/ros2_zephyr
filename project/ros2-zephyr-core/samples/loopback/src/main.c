#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dds/ddsrt/heap.h>
#include <phase4_test_msgs/msg/nested_fixed.h>
#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

typedef struct allocation_header_s
{
  size_t size;
} allocation_header_t;

typedef struct allocation_metrics_s
{
  size_t calls;
  size_t frees;
  size_t live_bytes;
  size_t high_water_bytes;
} allocation_metrics_t;

static allocation_metrics_t ros_metrics;
static allocation_metrics_t dds_metrics;
static volatile bool received;
static phase4_test_msgs__msg__NestedFixed received_message;

static void metrics_add(allocation_metrics_t * metrics, size_t size)
{
  metrics->calls++;
  metrics->live_bytes += size;
  if (metrics->live_bytes > metrics->high_water_bytes) {
    metrics->high_water_bytes = metrics->live_bytes;
  }
}

static void * tracked_allocate(size_t size, void * state)
{
  allocation_metrics_t * metrics = state;
  allocation_header_t * header = malloc(sizeof(*header) + size);
  if (header == NULL) {
    return NULL;
  }
  header->size = size;
  metrics_add(metrics, size);
  return header + 1;
}

static void tracked_deallocate(void * pointer, void * state)
{
  if (pointer == NULL) {
    return;
  }
  allocation_metrics_t * metrics = state;
  allocation_header_t * header = (allocation_header_t *)pointer - 1;
  metrics->frees++;
  metrics->live_bytes -= header->size;
  free(header);
}

static void * tracked_reallocate(void * pointer, size_t size, void * state)
{
  if (pointer == NULL) {
    return tracked_allocate(size, state);
  }
  allocation_metrics_t * metrics = state;
  allocation_header_t * old_header = (allocation_header_t *)pointer - 1;
  const size_t old_size = old_header->size;
  allocation_header_t * new_header = realloc(old_header, sizeof(*new_header) + size);
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

static void * tracked_zero_allocate(size_t count, size_t size, void * state)
{
  if (size != 0U && count > SIZE_MAX / size) {
    return NULL;
  }
  const size_t bytes = count * size;
  void * pointer = tracked_allocate(bytes, state);
  if (pointer != NULL) {
    memset(pointer, 0, bytes);
  }
  return pointer;
}

static void * dds_allocate(size_t size)
{
  return tracked_allocate(size, &dds_metrics);
}

static void * dds_zero_allocate(size_t count, size_t size)
{
  return tracked_zero_allocate(count, size, &dds_metrics);
}

static void * dds_reallocate(void * pointer, size_t size)
{
  return tracked_reallocate(pointer, size, &dds_metrics);
}

static void dds_deallocate(void * pointer)
{
  tracked_deallocate(pointer, &dds_metrics);
}

static void subscription_callback(const void * message)
{
  received_message = *(const phase4_test_msgs__msg__NestedFixed *)message;
  received = true;
}

static bool check(rcl_ret_t result, const char * operation)
{
  if (result == RCL_RET_OK) {
    return true;
  }
  printf("PHASE5_ERROR operation=%s code=%d detail=%s\n", operation, (int)result,
    rcl_get_error_string().str);
  rcl_reset_error();
  return false;
}

int main(void)
{
  printf("PHASE5_START board=%s path=rclc-rcl-rmw_cyclonedds_c-cyclonedds\n",
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
  rclc_support_t support;
  rcl_node_t node = rcl_get_zero_initialized_node();
  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
  phase4_test_msgs__msg__NestedFixed outgoing = {0};
  phase4_test_msgs__msg__NestedFixed incoming = {0};
  int status = 1;

  if (!check(rclc_support_init(&support, 0, NULL, &allocator), "support_init") ||
    !check(rclc_node_init_default(&node, "phase5_zephyr", "", &support), "node_init") ||
    !check(rclc_publisher_init_best_effort(
      &publisher, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(phase4_test_msgs, msg, NestedFixed),
      "phase5_loopback"), "publisher_init") ||
    !check(rclc_subscription_init_best_effort(
      &subscription, &node,
      ROSIDL_GET_MSG_TYPE_SUPPORT(phase4_test_msgs, msg, NestedFixed),
      "phase5_loopback"), "subscription_init") ||
    !check(rclc_executor_init(&executor, &support.context, 1U, &allocator), "executor_init") ||
    !check(rclc_executor_add_subscription(
      &executor, &subscription, &incoming, subscription_callback, ON_NEW_DATA),
      "executor_add_subscription"))
  {
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
      !check(rclc_executor_spin_some(&executor, RCL_MS_TO_NS(20)), "spin_some"))
    {
      goto cleanup;
    }
    k_sleep(K_MSEC(10));
  }

  if (!received || received_message.counter.data != outgoing.counter.data ||
    memcmp(received_message.samples.values, outgoing.samples.values,
      sizeof(outgoing.samples.values)) != 0)
  {
    printf("PHASE5_ERROR operation=loopback received=%d value=%" PRIu32 "\n",
      received, received_message.counter.data);
    goto cleanup;
  }

  printf("PHASE5_LOOPBACK_PASS value=%" PRIu32 " array=%" PRIu32 ",%" PRIu32
         ",%" PRIu32 ",%" PRIu32 "\n",
    received_message.counter.data,
    received_message.samples.values[0], received_message.samples.values[1],
    received_message.samples.values[2], received_message.samples.values[3]);
  printf("PHASE5_ALLOC setup_ros_calls=%zu steady_ros_calls=%zu"
         " setup_dds_calls=%zu steady_dds_calls=%zu"
         " ros_high_water=%zu dds_high_water=%zu\n",
    ros_setup_calls, ros_metrics.calls - ros_setup_calls,
    dds_setup_calls, dds_metrics.calls - dds_setup_calls,
    ros_metrics.high_water_bytes, dds_metrics.high_water_bytes);
  status = 0;

cleanup:
  (void)rclc_executor_fini(&executor);
  if (rcl_subscription_fini(&subscription, &node) != RCL_RET_OK) {
    status = 1;
  }
  if (rcl_publisher_fini(&publisher, &node) != RCL_RET_OK) {
    status = 1;
  }
  if (rcl_node_fini(&node) != RCL_RET_OK) {
    status = 1;
  }
  (void)rclc_support_fini(&support);
  printf("PHASE5_CLEANUP status=%d ros_live=%zu dds_live=%zu\n",
    status, ros_metrics.live_bytes, dds_metrics.live_bytes);
#ifdef CONFIG_ARCH_POSIX
  posix_exit(status);
#endif
  return status;
}
