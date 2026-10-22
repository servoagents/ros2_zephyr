// SPDX-License-Identifier: Apache-2.0
#include "generated_ros_init.h"
#include "wifi.h"
#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <ros2_zephyr/allocator.h>
#include <std_msgs/msg/u_int32.h>
#include <zephyr/kernel.h>

static ros2z_deployment_context_t deployment;
static bool publish_failed;

// Edit this callback to consume a command and publish a fixed-size state.
// No GPIO, motor-control timing or stale-command guarantee is implied.
void subscription_callback(const void *message)
{
  const std_msgs__msg__UInt32 *command = message;
  std_msgs__msg__UInt32 state = {.data = command->data};
  if (rcl_publish(&deployment.publisher_outgoing, &state, NULL) != RCL_RET_OK) {
    publish_failed = true;
  }
}

int main(void)
{
  int status = app_wifi_connect();
  if (status) {
    printk("C6_APP_WIFI_FAIL code=%d\n", status);
    return status;
  }
  rcl_allocator_t allocator = {
      .allocate = ros2_zephyr_allocate,
      .deallocate = ros2_zephyr_deallocate,
      .reallocate = ros2_zephyr_reallocate,
      .zero_allocate = ros2_zephyr_zero_allocate,
      .state = ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_ROS),
  };
  rclc_support_t support = {0};
  if (rclc_support_init(&support, 0, NULL, &allocator) != RCL_RET_OK) {
    printk("C6_APP_SUPPORT_FAIL\n");
    return 1;
  }
  if (ros2z_deployment_init(&deployment, &support, &allocator) != RCL_RET_OK) {
    printk("C6_APP_DEPLOYMENT_FAIL\n");
    (void)rclc_support_fini(&support);
    return 1;
  }
  printk("C6_APP_READY domain=73 command=/c6/command state=/c6/state\n");
  for (;;) {
    rcl_ret_t rc = rclc_executor_spin_some(&deployment.executor, RCL_MS_TO_NS(100));
    if ((rc != RCL_RET_OK && rc != RCL_RET_TIMEOUT) || publish_failed) {
      printk("C6_APP_FAIL code=%d publish_failed=%d\n", (int)rc, publish_failed);
      break;
    }
  }
  (void)ros2z_deployment_fini(&deployment);
  (void)rclc_support_fini(&support);
  return 1;
}
