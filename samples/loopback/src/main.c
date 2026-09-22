// SPDX-License-Identifier: Apache-2.0

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rcutils/logging.h>
#include <ros2_zephyr/allocator.h>
#include <ros2_zephyr_test_msgs/msg/nested_fixed.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

static volatile bool received;
static ros2_zephyr_test_msgs__msg__NestedFixed received_message;

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

static void subscription_callback(const void *message)
{
  received_message = *(const ros2_zephyr_test_msgs__msg__NestedFixed *)message;
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
  printf("ROS2_ZEPHYR_START board=%s rmw=%s middleware=%s\n", CONFIG_BOARD_TARGET,
         ROS2_ZEPHYR_RMW_NAME, ROS2_ZEPHYR_MIDDLEWARE_NAME);

  rcl_allocator_t allocator = {
      .allocate = ros2_zephyr_allocate,
      .deallocate = ros2_zephyr_deallocate,
      .reallocate = ros2_zephyr_reallocate,
      .zero_allocate = ros2_zephyr_zero_allocate,
      .state = ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_ROS),
  };
  rclc_support_t support = {0};
  rcl_node_t node = rcl_get_zero_initialized_node();
  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
  ros2_zephyr_test_msgs__msg__NestedFixed outgoing = {0};
  ros2_zephyr_test_msgs__msg__NestedFixed incoming = {0};
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
                 ROSIDL_GET_MSG_TYPE_SUPPORT(ros2_zephyr_test_msgs, msg, NestedFixed),
                 "ros2_zephyr_loopback"),
             "publisher_init")) {
    goto cleanup;
  }
  publisher_initialized = true;
  if (!check(rclc_subscription_init_best_effort(
                 &subscription, &node,
                 ROSIDL_GET_MSG_TYPE_SUPPORT(ros2_zephyr_test_msgs, msg, NestedFixed),
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
  if (!check(rclc_executor_prepare(&executor), "executor_prepare")) {
    goto cleanup;
  }

  const ros2_zephyr_allocation_metrics_t ros_setup =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_setup =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
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
  const ros2_zephyr_allocation_metrics_t ros_current =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_current =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
  printf("ROS2_ZEPHYR_ALLOC setup_ros_calls=%zu steady_ros_calls=%zu middleware=%s "
         "setup_middleware_calls=%zu steady_middleware_calls=%zu ros_high_water=%zu "
         "middleware_high_water=%zu\n",
         ros_setup.calls, ros_current.calls - ros_setup.calls, ROS2_ZEPHYR_MIDDLEWARE_NAME,
         middleware_setup.calls, middleware_current.calls - middleware_setup.calls,
         ros_current.high_water_bytes, middleware_current.high_water_bytes);
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
  if (rcutils_logging_shutdown() != RCUTILS_RET_OK) {
    status = 1;
  }
  const ros2_zephyr_allocation_metrics_t ros_final =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_final =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
  printf("ROS2_ZEPHYR_CLEANUP status=%d ros_live=%zu middleware_live=%zu middleware=%s\n",
         status, ros_final.live_bytes, middleware_final.live_bytes,
         ROS2_ZEPHYR_MIDDLEWARE_NAME);
#ifdef CONFIG_ARCH_POSIX
  posix_exit(status);
#endif
  return status;
}
