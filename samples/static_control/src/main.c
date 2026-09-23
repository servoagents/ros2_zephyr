// SPDX-License-Identifier: Apache-2.0

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rcutils/logging.h>
#include <rmw/qos_profiles.h>
#include <ros2_zephyr/allocator.h>
#include <ros2_zephyr_test_msgs/msg/control_command.h>
#include <ros2_zephyr_test_msgs/msg/control_state.h>
#include <ros2_zephyr_test_msgs/msg/detail/control_command__rosidl_typesupport_cyclonedds_c.h>
#include <ros2_zephyr_test_msgs/msg/detail/control_state__rosidl_typesupport_cyclonedds_c.h>
#include <rosidl_typesupport_interface/macros.h>
#include <zephyr/kernel.h>
#ifdef CONFIG_WIFI
#include <string.h>

#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>

#include "wifi_credentials.h"
#endif

#include "control.h"

enum {
  ROS_SPIN_TIMEOUT_MS = 20,
  TELEMETRY_PERIOD_MS = 100,
  REPORT_PERIOD_MS = 1000,
};

static uint32_t accepted_commands;
static uint32_t rejected_commands;

static bool check(rcl_ret_t result, const char *operation)
{
  if (result == RCL_RET_OK) {
    return true;
  }
  printf("STATIC_CONTROL_ERROR operation=%s code=%d detail=%s\n", operation, (int)result,
         rcl_get_error_string().str);
  rcl_reset_error();
  return false;
}

static const rosidl_message_type_support_t *command_type_support(void)
{
  return ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
      rosidl_typesupport_cyclonedds_c, ros2_zephyr_test_msgs, msg, ControlCommand)();
}

static const rosidl_message_type_support_t *state_type_support(void)
{
  return ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(
      rosidl_typesupport_cyclonedds_c, ros2_zephyr_test_msgs, msg, ControlState)();
}

static void command_callback(const void *message)
{
  const ros2_zephyr_test_msgs__msg__ControlCommand *ros_command = message;
  const struct static_control_command command = {
      .target = ros_command->target,
      .sequence = ros_command->sequence,
  };
  if (static_control_submit(&command, k_uptime_get())) {
    accepted_commands++;
  } else {
    rejected_commands++;
  }
}

#ifdef CONFIG_WIFI
static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(wifi_disconnected, 0, 1);
static K_SEM_DEFINE(ipv4_ready, 0, 1);
static struct net_mgmt_event_callback wifi_callback;
static struct net_mgmt_event_callback ipv4_callback;

static struct wifi_connect_req_params wifi_connect_params(void)
{
  return (struct wifi_connect_req_params){
      .ssid = (const uint8_t *)ROS2_ZEPHYR_WIFI_SSID,
      .ssid_length = strlen(ROS2_ZEPHYR_WIFI_SSID),
      .psk = (const uint8_t *)ROS2_ZEPHYR_WIFI_PSK,
      .psk_length = strlen(ROS2_ZEPHYR_WIFI_PSK),
      .channel = WIFI_CHANNEL_ANY,
      .security = WIFI_SECURITY_TYPE_PSK,
  };
}

static void wifi_event_handler(struct net_mgmt_event_callback *callback, uint64_t event,
                               struct net_if *iface)
{
  (void)iface;
  const struct wifi_status *status = callback->info;
  if (status == NULL || status->status != 0) {
    const char *operation =
        event == NET_EVENT_WIFI_DISCONNECT_RESULT ? "wifi_disconnect" : "wifi_connect";
    printf("STATIC_CONTROL_ERROR operation=%s status=%d\n", operation,
           status != NULL ? status->status : -1);
    return;
  }
  if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
    k_sem_give(&wifi_connected);
  } else if (event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
    k_sem_give(&wifi_disconnected);
  }
}

static void ipv4_event_handler(struct net_mgmt_event_callback *callback, uint64_t event,
                               struct net_if *iface)
{
  (void)callback;
  (void)iface;
  if (event == NET_EVENT_IPV4_ADDR_ADD) {
    k_sem_give(&ipv4_ready);
  }
}

static bool connect_network(void)
{
  struct net_if *iface = net_if_get_default();
  struct wifi_connect_req_params params = wifi_connect_params();

  net_mgmt_init_event_callback(&wifi_callback, wifi_event_handler,
                               NET_EVENT_WIFI_CONNECT_RESULT |
                                   NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&wifi_callback);
  net_mgmt_init_event_callback(&ipv4_callback, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
  net_mgmt_add_event_callback(&ipv4_callback);
  if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params)) != 0 ||
      k_sem_take(&wifi_connected, K_SECONDS(30)) != 0 ||
      k_sem_take(&ipv4_ready, K_SECONDS(30)) != 0) {
    printf("STATIC_CONTROL_ERROR operation=network_ready\n");
    return false;
  }

  struct wifi_ps_params power_save = {
      .enabled = WIFI_PS_DISABLED,
      .type = WIFI_PS_PARAM_STATE,
  };
  if (net_mgmt(NET_REQUEST_WIFI_PS, iface, &power_save, sizeof(power_save)) != 0) {
    printf("STATIC_CONTROL_ERROR operation=wifi_disable_power_save\n");
    return false;
  }
  const struct net_in_addr *address = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
  char address_text[NET_IPV4_ADDR_LEN];
  if (address == NULL ||
      net_addr_ntop(NET_AF_INET, address, address_text, sizeof(address_text)) == NULL) {
    printf("STATIC_CONTROL_ERROR operation=wifi_ipv4_address\n");
    return false;
  }
  printf("STATIC_CONTROL_NETWORK_READY address=%s\n", address_text);
  return true;
}

#ifdef CONFIG_STATIC_CONTROL_WIFI_RECONNECT_PROBE
enum {
  WIFI_RECONNECT_PROBE_DELAY_MS = 10000,
  WIFI_RECONNECT_PROBE_OFFLINE_MS = 5000,
  WIFI_RECONNECT_PROBE_TIMEOUT_SECONDS = 30,
  WIFI_RECONNECT_PROBE_STACK_SIZE = 2048,
};

K_THREAD_STACK_DEFINE(wifi_reconnect_probe_stack, WIFI_RECONNECT_PROBE_STACK_SIZE);
static struct k_thread wifi_reconnect_probe_thread;

static void wifi_reconnect_probe(void *first, void *second, void *third)
{
  (void)first;
  (void)second;
  (void)third;
  struct net_if *iface = net_if_get_default();
  struct wifi_connect_req_params params = wifi_connect_params();

  k_sleep(K_MSEC(WIFI_RECONNECT_PROBE_DELAY_MS));
  printf("STATIC_CONTROL_WIFI_PROBE phase=disconnecting\n");
  if (net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0) != 0 ||
      k_sem_take(&wifi_disconnected, K_SECONDS(WIFI_RECONNECT_PROBE_TIMEOUT_SECONDS)) != 0) {
    printf("STATIC_CONTROL_ERROR operation=wifi_probe_disconnect\n");
    return;
  }
  printf("STATIC_CONTROL_WIFI_PROBE phase=disconnected\n");

  k_sleep(K_MSEC(WIFI_RECONNECT_PROBE_OFFLINE_MS));
  if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params)) != 0 ||
      k_sem_take(&wifi_connected, K_SECONDS(WIFI_RECONNECT_PROBE_TIMEOUT_SECONDS)) != 0 ||
      k_sem_take(&ipv4_ready, K_SECONDS(WIFI_RECONNECT_PROBE_TIMEOUT_SECONDS)) != 0) {
    printf("STATIC_CONTROL_ERROR operation=wifi_probe_reconnect\n");
    return;
  }
  printf("STATIC_CONTROL_WIFI_PROBE phase=reconnected\n");
}

static void start_wifi_reconnect_probe(void)
{
  (void)k_thread_create(&wifi_reconnect_probe_thread, wifi_reconnect_probe_stack,
                        K_THREAD_STACK_SIZEOF(wifi_reconnect_probe_stack), wifi_reconnect_probe,
                        NULL, NULL, NULL, 5, 0, K_NO_WAIT);
  k_thread_name_set(&wifi_reconnect_probe_thread, "wifi_reconnect_probe");
}
#endif
#else
static bool connect_network(void) { return true; }
#endif

int main(void)
{
  printf("STATIC_CONTROL_START board=%s period_ms=10 expiry_ms=250 deadline_ms=10\n",
         CONFIG_BOARD_TARGET);
  if (!connect_network()) {
    return 1;
  }

  rcl_allocator_t allocator = {
      .allocate = ros2_zephyr_allocate,
      .deallocate = ros2_zephyr_deallocate,
      .reallocate = ros2_zephyr_reallocate,
      .zero_allocate = ros2_zephyr_zero_allocate,
      .state = ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_ROS),
  };
  rclc_support_t support = {0};
  rcl_node_t node = rcl_get_zero_initialized_node();
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
  ros2_zephyr_test_msgs__msg__ControlCommand command_message = {0};
  bool support_initialized = false;
  bool node_initialized = false;
  bool subscription_initialized = false;
  bool publisher_initialized = false;
  bool executor_initialized = false;
  bool control_started = false;
  int result = 1;

  if (!check(rclc_support_init(&support, 0, NULL, &allocator), "support_init")) {
    goto cleanup;
  }
  support_initialized = true;
  if (!check(rclc_node_init_default(&node, "static_control", "", &support), "node_init")) {
    goto cleanup;
  }
  node_initialized = true;
  rmw_qos_profile_t command_qos = rmw_qos_profile_sensor_data;
  command_qos.depth = 1U;
  if (!check(rclc_subscription_init(&subscription, &node, command_type_support(),
                                    "static_control/command", &command_qos),
             "subscription_init")) {
    goto cleanup;
  }
  subscription_initialized = true;
  rmw_qos_profile_t state_qos = rmw_qos_profile_sensor_data;
  state_qos.depth = 1U;
  if (!check(rclc_publisher_init(&publisher, &node, state_type_support(), "static_control/state",
                                 &state_qos),
             "publisher_init")) {
    goto cleanup;
  }
  publisher_initialized = true;
  if (!check(rclc_executor_init(&executor, &support.context, 1U, &allocator), "executor_init")) {
    goto cleanup;
  }
  executor_initialized = true;
  if (!check(rclc_executor_add_subscription(&executor, &subscription, &command_message,
                                            command_callback, ON_NEW_DATA),
             "executor_add_subscription") ||
      !check(rclc_executor_prepare(&executor), "executor_prepare")) {
    goto cleanup;
  }
  if (!static_control_start()) {
    printf("STATIC_CONTROL_ERROR operation=control_start\n");
    goto cleanup;
  }
  control_started = true;

  const ros2_zephyr_allocation_metrics_t ros_prepared =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_prepared =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
  printf("STATIC_CONTROL_READY ros_alloc_calls=%zu middleware_alloc_calls=%zu\n",
         ros_prepared.calls, middleware_prepared.calls);
#ifdef CONFIG_STATIC_CONTROL_WIFI_RECONNECT_PROBE
  start_wifi_reconnect_probe();
#endif

  int64_t next_telemetry_ms = k_uptime_get();
  int64_t next_report_ms = next_telemetry_ms + REPORT_PERIOD_MS;
  uint8_t previous_status = UINT8_MAX;
  for (;;) {
    const rcl_ret_t spin = rclc_executor_spin_some(&executor, RCL_MS_TO_NS(ROS_SPIN_TIMEOUT_MS));
    if (spin != RCL_RET_OK && spin != RCL_RET_TIMEOUT) {
      (void)check(spin, "spin_some");
      k_sleep(K_MSEC(ROS_SPIN_TIMEOUT_MS));
    }

    const int64_t now_ms = k_uptime_get();
    if (now_ms >= next_telemetry_ms) {
      const struct static_control_state state = static_control_read_state();
      ros2_zephyr_test_msgs__msg__ControlState ros_state = {
          .position = state.position,
          .velocity = state.velocity,
          .effort = state.effort,
          .command_sequence = state.command_sequence,
          .control_cycles = state.control_cycles,
          .skipped_releases = state.skipped_releases,
          .status = state.status,
      };
      const rcl_ret_t publish = rcl_publish(&publisher, &ros_state, NULL);
      if (publish != RCL_RET_OK) {
        (void)check(publish, "publish_state");
      }
      next_telemetry_ms = now_ms + TELEMETRY_PERIOD_MS;
      if (state.status != previous_status) {
        printf("STATIC_CONTROL_STATUS status=%u sequence=%" PRIu32 " cycles=%" PRIu32
               " skipped=%" PRIu32 "\n",
               state.status, state.command_sequence, state.control_cycles, state.skipped_releases);
        previous_status = state.status;
      }
    }
    if (now_ms >= next_report_ms) {
      const struct static_control_state state = static_control_read_state();
      const ros2_zephyr_allocation_metrics_t ros_now =
          ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
      const ros2_zephyr_allocation_metrics_t middleware_now =
          ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
      size_t control_stack_unused = 0U;
      const bool stack_measured = static_control_stack_space(&control_stack_unused);
      printf("STATIC_CONTROL_METRICS accepted=%" PRIu32 " rejected=%" PRIu32
             " ros_steady_alloc_calls=%zu middleware_steady_alloc_calls=%zu"
             " ros_high_water=%zu middleware_high_water=%zu"
             " max_lateness_us=%" PRIu32 " skipped=%" PRIu32 " control_stack_unused=%s",
             accepted_commands, rejected_commands, ros_now.calls - ros_prepared.calls,
             middleware_now.calls - middleware_prepared.calls, ros_now.high_water_bytes,
             middleware_now.high_water_bytes, state.max_release_lateness_us,
             state.skipped_releases,
             stack_measured ? "measured" : "unavailable");
      if (stack_measured) {
        printf(" unused_bytes=%zu", control_stack_unused);
      }
      printf("\n");
      next_report_ms = now_ms + REPORT_PERIOD_MS;
    }
  }

cleanup:
  if (control_started) {
    static_control_stop();
  }
  if (executor_initialized && rclc_executor_fini(&executor) != RCL_RET_OK) {
    result = 1;
  }
  if (publisher_initialized && rcl_publisher_fini(&publisher, &node) != RCL_RET_OK) {
    result = 1;
  }
  if (subscription_initialized && rcl_subscription_fini(&subscription, &node) != RCL_RET_OK) {
    result = 1;
  }
  if (node_initialized && rcl_node_fini(&node) != RCL_RET_OK) {
    result = 1;
  }
  if (support_initialized && rclc_support_fini(&support) != RCL_RET_OK) {
    result = 1;
  }
  if (rcutils_logging_shutdown() != RCUTILS_RET_OK) {
    result = 1;
  }
  const ros2_zephyr_allocation_metrics_t ros_final =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_final =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
  printf("STATIC_CONTROL_CLEANUP result=%d ros_live=%zu middleware_live=%zu\n", result,
         ros_final.live_bytes, middleware_final.live_bytes);
  return result;
}
