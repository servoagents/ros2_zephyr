// SPDX-License-Identifier: Apache-2.0

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <rcl/error_handling.h>
#include <rcl/graph.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rcutils/logging.h>
#include <rcutils/types/string_array.h>
#include <rmw/qos_profiles.h>
#include <ros2_zephyr/allocator.h>
#if defined(CONFIG_ROS2_ZEPHYR_RMW_ZENOH_PICO)
#include <std_msgs/msg/detail/u_int32__rosidl_typesupport_microxrcedds_c.h>
#else
#include <std_msgs/msg/detail/u_int32__rosidl_typesupport_introspection_c.h>
#endif
#include <std_msgs/msg/u_int32.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/sys_heap.h>

#include "wifi_credentials.h"

enum {
  MATCH_TIMEOUT_MS = 60000,
  UNOBSERVABLE_MATCH_GRACE_MS = 5000,
  EXPECTED_SAMPLE_COUNT = 18,
  DESKTOP_TO_DEVICE_VALUE = 314159265U,
  DEVICE_TO_DESKTOP_VALUE = 271828182U,
  TRANSIENT_HISTORY_LAST_VALUE = 5U,
  TRANSIENT_LIVE_VALUE = 6U,
  GRAPH_INITIAL_HOLD_MS = 15000,
  GRAPH_TRANSITION_HOLD_MS = 8000,
  GRAPH_PHASE_TIMEOUT_MS = 60000,
};

static K_SEM_DEFINE(wifi_connected, 0, 1);
static K_SEM_DEFINE(ipv4_ready, 0, 1);
static struct net_mgmt_event_callback wifi_callback;
static struct net_mgmt_event_callback ipv4_callback;
static bool received_invalid_value;
static uint32_t received_value;
static unsigned int received_count;

static rmw_qos_profile_t wifi_qos(void)
{
  rmw_qos_profile_t qos = rmw_qos_profile_sensor_data;
  qos.depth = ROS2_ZEPHYR_WIFI_DEPTH;
#if defined(ROS2_ZEPHYR_WIFI_RELIABILITY_reliable)
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
#endif
#if defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
  qos.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
#endif
  return qos;
}

static const char *wifi_reliability_name(void)
{
#if defined(ROS2_ZEPHYR_WIFI_RELIABILITY_reliable)
  return "reliable";
#else
  return "best_effort";
#endif
}

static const char *wifi_durability_name(void)
{
#if defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
  return "transient_local";
#else
  return "volatile";
#endif
}

static bool wifi_actual_qos_matches(const rmw_qos_profile_t *actual,
                                    const rmw_qos_profile_t *requested)
{
  return actual != NULL && actual->history == RMW_QOS_POLICY_HISTORY_KEEP_LAST &&
         actual->depth == requested->depth && actual->reliability == requested->reliability &&
         actual->durability == requested->durability;
}

static unsigned int expected_receive_count(void)
{
#if defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
  const unsigned int retained = ROS2_ZEPHYR_WIFI_DEPTH < TRANSIENT_HISTORY_LAST_VALUE
                                    ? ROS2_ZEPHYR_WIFI_DEPTH
                                    : TRANSIENT_HISTORY_LAST_VALUE;
  return retained + 1U;
#else
  return EXPECTED_SAMPLE_COUNT;
#endif
}

static const rosidl_message_type_support_t *uint32_type_support(void)
{
#if defined(CONFIG_ROS2_ZEPHYR_RMW_ZENOH_PICO)
  return ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_microxrcedds_c,
                                                           std_msgs, msg, UInt32)();
#else
  return ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c,
                                                           std_msgs, msg, UInt32)();
#endif
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

static void wifi_event_handler(struct net_mgmt_event_callback *callback, uint64_t event,
                               struct net_if *iface)
{
  (void)iface;
  if (event != NET_EVENT_WIFI_CONNECT_RESULT) {
    return;
  }
  const struct wifi_status *status = callback->info;
  if (status != NULL && status->status == 0) {
    k_sem_give(&wifi_connected);
  } else {
    printf("ROS2_ZEPHYR_ERROR operation=wifi_connect status=%d\n",
           status != NULL ? status->status : -1);
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

static bool connect_wifi(void)
{
  struct net_if *iface = net_if_get_default();
  struct wifi_connect_req_params params = {
      .ssid = (const uint8_t *)ROS2_ZEPHYR_WIFI_SSID,
      .ssid_length = strlen(ROS2_ZEPHYR_WIFI_SSID),
      .psk = (const uint8_t *)ROS2_ZEPHYR_WIFI_PSK,
      .psk_length = strlen(ROS2_ZEPHYR_WIFI_PSK),
      .channel = WIFI_CHANNEL_ANY,
      .security = WIFI_SECURITY_TYPE_PSK,
  };

  net_mgmt_init_event_callback(&wifi_callback, wifi_event_handler, NET_EVENT_WIFI_CONNECT_RESULT);
  net_mgmt_add_event_callback(&wifi_callback);
  net_mgmt_init_event_callback(&ipv4_callback, ipv4_event_handler, NET_EVENT_IPV4_ADDR_ADD);
  net_mgmt_add_event_callback(&ipv4_callback);

  if (net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params)) != 0) {
    printf("ROS2_ZEPHYR_ERROR operation=wifi_connect_request\n");
    return false;
  }
  if (k_sem_take(&wifi_connected, K_SECONDS(30)) != 0) {
    printf("ROS2_ZEPHYR_ERROR operation=wifi_connect_timeout\n");
    return false;
  }
  if (k_sem_take(&ipv4_ready, K_SECONDS(30)) != 0) {
    printf("ROS2_ZEPHYR_ERROR operation=dhcp_timeout\n");
    return false;
  }

  struct wifi_ps_params power_save = {
      .enabled = WIFI_PS_DISABLED,
      .type = WIFI_PS_PARAM_STATE,
  };
  if (net_mgmt(NET_REQUEST_WIFI_PS, iface, &power_save, sizeof(power_save)) != 0) {
    printf("ROS2_ZEPHYR_ERROR operation=wifi_disable_power_save\n");
    return false;
  }

  const struct net_in_addr *address =
      net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
  char address_text[NET_IPV4_ADDR_LEN];
  if (address == NULL ||
      net_addr_ntop(NET_AF_INET, address, address_text, sizeof(address_text)) == NULL) {
    printf("ROS2_ZEPHYR_ERROR operation=wifi_ipv4_address\n");
    return false;
  }

  printf("ROS2_ZEPHYR_WIFI_READY power_save=disabled address=%s\n", address_text);
  return true;
}

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

static void report_resources(void)
{
  struct thread_metrics thread_metrics = {0};
  k_thread_foreach(report_thread, &thread_metrics);
  printf("ROS2_ZEPHYR_STACK_TOTAL threads=%zu reserved=%zu\n", thread_metrics.count,
         thread_metrics.reserved_bytes);

  struct sys_heap **heaps = NULL;
  const int heap_count = sys_heap_array_get(&heaps);
  for (int index = 0; index < heap_count; ++index) {
    struct sys_memory_stats stats;
    if (sys_heap_runtime_stats_get(heaps[index], &stats) == 0) {
      printf("ROS2_ZEPHYR_HEAP index=%d managed=%zu allocated=%zu free=%zu peak=%zu\n", index,
             stats.allocated_bytes + stats.free_bytes, stats.allocated_bytes, stats.free_bytes,
             stats.max_allocated_bytes);
    }
  }
  const ros2_zephyr_allocation_metrics_t ros_snapshot =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_snapshot =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
  printf("ROS2_ZEPHYR_ALLOC ros_calls=%zu ros_high_water=%zu middleware=%s "
         "middleware_calls=%zu middleware_high_water=%zu\n",
         ros_snapshot.calls, ros_snapshot.high_water_bytes, ROS2_ZEPHYR_MIDDLEWARE_NAME,
         middleware_snapshot.calls, middleware_snapshot.high_water_bytes);
}

typedef struct graph_expectation_s {
  const char *phase;
  bool alpha_node;
  bool beta_node;
  bool topic_a;
  bool topic_b;
  size_t topic_a_publishers;
  size_t topic_a_subscribers;
} graph_expectation_t;

static bool graph_has_node(const rcutils_string_array_t *names,
                           const rcutils_string_array_t *namespaces, const char *name,
                           const char *namespace_)
{
  for (size_t index = 0U; index < names->size; ++index) {
    if (strcmp(names->data[index], name) == 0 &&
        strcmp(namespaces->data[index], namespace_) == 0) {
      return true;
    }
  }
  return false;
}

static bool graph_has_topic_type(const rcl_names_and_types_t *topics, const char *topic,
                                 const char *type)
{
  for (size_t topic_index = 0U; topic_index < topics->names.size; ++topic_index) {
    if (strcmp(topics->names.data[topic_index], topic) != 0) {
      continue;
    }
    for (size_t type_index = 0U; type_index < topics->types[topic_index].size; ++type_index) {
      if (strcmp(topics->types[topic_index].data[type_index], type) == 0) {
        return true;
      }
    }
  }
  return false;
}

static bool graph_matches(rcl_node_t *node, rcl_allocator_t *allocator,
                          const graph_expectation_t *expected)
{
  rcutils_string_array_t names = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t namespaces = rcutils_get_zero_initialized_string_array();
  rcl_names_and_types_t topics = rcl_get_zero_initialized_names_and_types();
  bool names_initialized = false;
  bool topics_initialized = false;
  bool matches = false;
  size_t publishers = 0U;
  size_t subscribers = 0U;

  if (!check(rcl_get_node_names(node, *allocator, &names, &namespaces),
             "graph_get_node_names")) {
    goto cleanup;
  }
  names_initialized = true;
  if (!check(rcl_get_topic_names_and_types(node, allocator, false, &topics),
             "graph_get_topic_names_and_types")) {
    goto cleanup;
  }
  topics_initialized = true;
  if (!check(rcl_count_publishers(node, "/ros2_zephyr/graph_remote_a", &publishers),
             "graph_count_publishers")) {
    goto cleanup;
  }
  if (!check(rcl_count_subscribers(node, "/ros2_zephyr/graph_remote_a", &subscribers),
             "graph_count_subscribers")) {
    goto cleanup;
  }

  matches = graph_has_node(&names, &namespaces, "graph_peer_alpha", "/graph_acceptance") ==
                expected->alpha_node &&
            graph_has_node(&names, &namespaces, "graph_peer_beta", "/graph_acceptance_alt") ==
                expected->beta_node &&
            graph_has_topic_type(&topics, "/ros2_zephyr/graph_remote_a",
                                 "std_msgs/msg/UInt32") == expected->topic_a &&
            graph_has_topic_type(&topics, "/ros2_zephyr/graph_remote_b",
                                 "std_msgs/msg/UInt32") == expected->topic_b &&
            publishers == expected->topic_a_publishers &&
            subscribers == expected->topic_a_subscribers;

cleanup:
  if (topics_initialized && !check(rcl_names_and_types_fini(&topics), "graph_topic_names_fini")) {
    matches = false;
  }
  if (names_initialized) {
    if (rcutils_string_array_fini(&names) != RCUTILS_RET_OK ||
        rcutils_string_array_fini(&namespaces) != RCUTILS_RET_OK) {
      printf("ROS2_ZEPHYR_ERROR operation=graph_node_names_fini\n");
      matches = false;
    }
  }
  return matches;
}

static bool wait_for_graph_phase(rcl_node_t *node, rcl_allocator_t *allocator,
                                 const graph_expectation_t *expected)
{
  const int64_t start = k_uptime_get();
  while (k_uptime_get() - start < GRAPH_PHASE_TIMEOUT_MS) {
    if (graph_matches(node, allocator, expected)) {
      printf("ROS2_ZEPHYR_GRAPH_PASS phase=%s elapsed_ms=%" PRId64 "\n", expected->phase,
             k_uptime_get() - start);
      return true;
    }
    k_sleep(K_MSEC(100));
  }
  printf("ROS2_ZEPHYR_ERROR operation=graph_phase_timeout phase=%s\n", expected->phase);
  return false;
}

static int __attribute__((unused)) run_graph_node(rcl_node_t *node, rcl_allocator_t *allocator)
{
  static const graph_expectation_t phases[] = {
      {.phase = "initial",
       .alpha_node = true,
       .beta_node = true,
       .topic_a = true,
       .topic_b = true,
       .topic_a_publishers = 2U,
       .topic_a_subscribers = 2U},
      {.phase = "reduced",
       .alpha_node = true,
       .beta_node = false,
       .topic_a = true,
       .topic_b = false,
       .topic_a_publishers = 1U,
       .topic_a_subscribers = 0U},
      {.phase = "participant_lost"},
      {.phase = "restart",
       .alpha_node = true,
       .topic_a = true,
       .topic_a_subscribers = 1U},
      {.phase = "restart_lost"},
  };

  printf("ROS2_ZEPHYR_READY role=node domain=%d\n", CONFIG_ROS2_ZEPHYR_DOMAIN_ID);
  for (size_t index = 0U; index < ARRAY_SIZE(phases); ++index) {
    if (!wait_for_graph_phase(node, allocator, &phases[index])) {
      return 1;
    }
    if (index == 0U) {
      /* The local node and each remote node use distinct DDS participants. */
      printf("ROS2_ZEPHYR_GRAPH_CACHE_EVIDENCE participants=3 nodes=3 endpoints=5 "
             "source=validated_topology\n");
    }
  }
  return 0;
}

static int __attribute__((unused)) run_graph_pubsub(rcl_node_t *node)
{
  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  const rmw_qos_profile_t qos = wifi_qos();
  bool publisher_initialized = false;
  bool subscription_initialized = false;
  int result = 1;

  if (!check(rclc_publisher_init(&publisher, node, uint32_type_support(),
                                 "ros2_zephyr/graph_local", &qos),
             "graph_publisher_init")) {
    goto cleanup;
  }
  publisher_initialized = true;
  if (!check(rclc_subscription_init(&subscription, node, uint32_type_support(),
                                    "ros2_zephyr/graph_local", &qos),
             "graph_subscription_init")) {
    goto cleanup;
  }
  subscription_initialized = true;

  printf("ROS2_ZEPHYR_GRAPH_LOCAL phase=pubsub\n");
  printf("ROS2_ZEPHYR_READY role=pubsub domain=%d\n", CONFIG_ROS2_ZEPHYR_DOMAIN_ID);
  k_sleep(K_MSEC(GRAPH_INITIAL_HOLD_MS));
  if (rcl_publisher_fini(&publisher, node) != RCL_RET_OK) {
    printf("ROS2_ZEPHYR_ERROR operation=graph_publisher_fini\n");
    goto cleanup;
  }
  publisher_initialized = false;
  printf("ROS2_ZEPHYR_GRAPH_LOCAL phase=subscription_only\n");
  k_sleep(K_MSEC(GRAPH_TRANSITION_HOLD_MS));
  if (rcl_subscription_fini(&subscription, node) != RCL_RET_OK) {
    printf("ROS2_ZEPHYR_ERROR operation=graph_subscription_fini\n");
    goto cleanup;
  }
  subscription_initialized = false;
  printf("ROS2_ZEPHYR_GRAPH_LOCAL phase=node_only\n");
  k_sleep(K_MSEC(GRAPH_TRANSITION_HOLD_MS));
  result = 0;

cleanup:
  if (subscription_initialized && rcl_subscription_fini(&subscription, node) != RCL_RET_OK) {
    result = 1;
  }
  if (publisher_initialized && rcl_publisher_fini(&publisher, node) != RCL_RET_OK) {
    result = 1;
  }
  return result;
}

static void subscription_callback(const void *message)
{
  const std_msgs__msg__UInt32 *sample = message;
  if (sample != NULL) {
#if defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
    const unsigned int retained = ROS2_ZEPHYR_WIFI_DEPTH < TRANSIENT_HISTORY_LAST_VALUE
                                      ? ROS2_ZEPHYR_WIFI_DEPTH
                                      : TRANSIENT_HISTORY_LAST_VALUE;
    const uint32_t expected = TRANSIENT_LIVE_VALUE - retained + received_count;
#else
    const uint32_t expected = DESKTOP_TO_DEVICE_VALUE;
#endif
    received_value = sample->data;
    received_count++;
    if (sample->data != expected) {
      received_invalid_value = true;
    }
    printf("ROS2_ZEPHYR_RECEIVED direction=desktop_to_device value=%" PRIu32 " sequence=%u\n",
           sample->data, received_count);
  }
}

static int __attribute__((unused)) run_publisher(rcl_node_t *node)
{
#if !defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
  static const struct {
    unsigned int rate_hz;
    unsigned int interval_ms;
    unsigned int samples;
  } rate_cases[] = {
      {1U, 1000U, 3U},
      {10U, 100U, 5U},
      {100U, 10U, 10U},
  };
#endif

  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  const rmw_qos_profile_t qos = wifi_qos();
  if (!check(rclc_publisher_init(&publisher, node, uint32_type_support(),
                                 "ros2_zephyr/device_to_desktop", &qos),
             "publisher_init")) {
    return 1;
  }
  const rmw_qos_profile_t *actual_qos = rcl_publisher_get_actual_qos(&publisher);
  if (!wifi_actual_qos_matches(actual_qos, &qos)) {
    printf("ROS2_ZEPHYR_ERROR operation=publisher_actual_qos"
           " actual_history=%d actual_depth=%zu actual_reliability=%d actual_durability=%d"
           " requested_history=%d requested_depth=%zu requested_reliability=%d"
           " requested_durability=%d\n",
           actual_qos != NULL ? (int)actual_qos->history : -1,
           actual_qos != NULL ? actual_qos->depth : 0U,
           actual_qos != NULL ? (int)actual_qos->reliability : -1,
           actual_qos != NULL ? (int)actual_qos->durability : -1, (int)qos.history, qos.depth,
           (int)qos.reliability, (int)qos.durability);
    if (rcl_publisher_fini(&publisher, node) != RCL_RET_OK) {
      printf("ROS2_ZEPHYR_ERROR operation=publisher_fini\n");
    }
    return 1;
  }
  printf("ROS2_ZEPHYR_QOS role=pub reliability=%s durability=%s depth=%zu\n",
         wifi_reliability_name(), wifi_durability_name(), qos.depth);

  int result = 1;
#if defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
  for (uint32_t value = 1U; value <= TRANSIENT_HISTORY_LAST_VALUE; ++value) {
    const std_msgs__msg__UInt32 historical = {.data = value};
    if (!check(rcl_publish(&publisher, &historical, NULL), "publish_history")) {
      goto cleanup;
    }
    printf("ROS2_ZEPHYR_SENT phase=history value=%" PRIu32 "\n", value);
  }
  printf("ROS2_ZEPHYR_HISTORY_READY role=pub depth=%zu history_last=%u\n", qos.depth,
         TRANSIENT_HISTORY_LAST_VALUE);
#endif
  size_t matched = 0U;
  bool match_count_supported = true;
  const int64_t discovery_start = k_uptime_get();
  printf("ROS2_ZEPHYR_READY role=pub domain=%d\n", CONFIG_ROS2_ZEPHYR_DOMAIN_ID);
  while (k_uptime_get() - discovery_start < MATCH_TIMEOUT_MS && matched == 0U) {
    const rcl_ret_t count_result =
        rcl_publisher_get_subscription_count(&publisher, &matched);
    if (count_result == RCL_RET_UNSUPPORTED) {
      rcl_reset_error();
      match_count_supported = false;
      k_sleep(K_MSEC(500));
      break;
    }
    if (!check(count_result, "publisher_get_subscription_count")) {
      goto cleanup;
    }
    k_sleep(K_MSEC(20));
  }
  if (match_count_supported && matched == 0U) {
    printf("ROS2_ZEPHYR_ERROR operation=publisher_match_timeout\n");
    goto cleanup;
  }

  if (match_count_supported) {
    printf("ROS2_ZEPHYR_MATCH role=pub peers=%zu discovery_ms=%" PRId64 "\n", matched,
           k_uptime_get() - discovery_start);
    k_sleep(K_MSEC(500));
  } else {
    printf("ROS2_ZEPHYR_MATCH role=pub peers=unknown discovery_ms=unavailable\n");
    k_sleep(K_MSEC(UNOBSERVABLE_MATCH_GRACE_MS));
  }
#if defined(ROS2_ZEPHYR_WIFI_DURABILITY_transient_local)
  const std_msgs__msg__UInt32 live_message = {.data = TRANSIENT_LIVE_VALUE};
  if (!check(rcl_publish(&publisher, &live_message, NULL), "publish_live")) {
    goto cleanup;
  }
  printf("ROS2_ZEPHYR_SENT phase=live value=%u\n", TRANSIENT_LIVE_VALUE);
#else
  const std_msgs__msg__UInt32 message = {.data = DEVICE_TO_DESKTOP_VALUE};
  for (size_t rate_index = 0U; rate_index < ARRAY_SIZE(rate_cases); ++rate_index) {
    for (unsigned int sequence = 1U; sequence <= rate_cases[rate_index].samples; ++sequence) {
      const uint64_t publish_start = k_cycle_get_64();
      if (!check(rcl_publish(&publisher, &message, NULL), "publish")) {
        goto cleanup;
      }
      printf("ROS2_ZEPHYR_SENT direction=device_to_desktop value=%" PRIu32
             " rate_hz=%u sequence=%u publish_call_ns=%" PRIu64 "\n",
             message.data, rate_cases[rate_index].rate_hz, sequence,
             k_cyc_to_ns_floor64(k_cycle_get_64() - publish_start));
      k_sleep(K_MSEC(rate_cases[rate_index].interval_ms));
    }
  }
#endif
#if defined(ROS2_ZEPHYR_WIFI_RELIABILITY_reliable)
  /* This RMW does not implement wait-for-all-acked; allow the final heartbeat/ACKNACK round. */
  k_sleep(K_SECONDS(2));
#endif
  result = 0;

cleanup:
  if (rcl_publisher_fini(&publisher, node) != RCL_RET_OK) {
    result = 1;
  }
  return result;
}

static int __attribute__((unused)) run_subscriber(rcl_node_t *node, rclc_support_t *support,
                                                  rcl_allocator_t *allocator)
{
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
  std_msgs__msg__UInt32 message = {0};
  const rmw_qos_profile_t qos = wifi_qos();
  if (!check(rclc_subscription_init(&subscription, node, uint32_type_support(),
                                    "ros2_zephyr/desktop_to_device", &qos),
             "subscription_init")) {
    return 1;
  }
  const rmw_qos_profile_t *actual_qos = rcl_subscription_get_actual_qos(&subscription);
  if (!wifi_actual_qos_matches(actual_qos, &qos)) {
    printf("ROS2_ZEPHYR_ERROR operation=subscription_actual_qos"
           " actual_history=%d actual_depth=%zu actual_reliability=%d actual_durability=%d"
           " requested_history=%d requested_depth=%zu requested_reliability=%d"
           " requested_durability=%d\n",
           actual_qos != NULL ? (int)actual_qos->history : -1,
           actual_qos != NULL ? actual_qos->depth : 0U,
           actual_qos != NULL ? (int)actual_qos->reliability : -1,
           actual_qos != NULL ? (int)actual_qos->durability : -1, (int)qos.history, qos.depth,
           (int)qos.reliability, (int)qos.durability);
    if (rcl_subscription_fini(&subscription, node) != RCL_RET_OK) {
      printf("ROS2_ZEPHYR_ERROR operation=subscription_fini\n");
    }
    return 1;
  }
  printf("ROS2_ZEPHYR_QOS role=sub reliability=%s durability=%s depth=%zu\n",
         wifi_reliability_name(), wifi_durability_name(), qos.depth);

  int result = 1;
  bool executor_initialized = false;
  if (!check(rclc_executor_init(&executor, &support->context, 1U, allocator), "executor_init")) {
    goto cleanup;
  }
  executor_initialized = true;
  if (!check(rclc_executor_add_subscription(&executor, &subscription, &message,
                                            subscription_callback, ON_NEW_DATA),
             "executor_add_subscription")) {
    goto cleanup;
  }
  if (!check(rclc_executor_prepare(&executor), "executor_prepare")) {
    goto cleanup;
  }

  size_t matched = 0U;
  bool match_reported = false;
  bool match_count_supported = true;
  const int64_t discovery_start = k_uptime_get();
  int64_t receive_deadline = discovery_start + MATCH_TIMEOUT_MS;
  printf("ROS2_ZEPHYR_READY role=sub domain=%d\n", CONFIG_ROS2_ZEPHYR_DOMAIN_ID);
  while (k_uptime_get() < receive_deadline && received_count < expected_receive_count()) {
    const rcl_ret_t spin_result = rclc_executor_spin_some(&executor, RCL_MS_TO_NS(20));
    if (spin_result != RCL_RET_OK && spin_result != RCL_RET_TIMEOUT) {
      check(spin_result, "spin_some");
      goto cleanup;
    }
    if (!match_reported && match_count_supported) {
      const rcl_ret_t count_result =
          rcl_subscription_get_publisher_count(&subscription, &matched);
      if (count_result == RCL_RET_UNSUPPORTED) {
        rcl_reset_error();
        match_count_supported = false;
        printf("ROS2_ZEPHYR_MATCH role=sub peers=unknown discovery_ms=unavailable\n");
      } else if (!check(count_result, "subscription_get_publisher_count")) {
        goto cleanup;
      }
      if (matched > 0U) {
        match_reported = true;
        receive_deadline = k_uptime_get() + MATCH_TIMEOUT_MS;
        printf("ROS2_ZEPHYR_MATCH role=sub peers=%zu discovery_ms=%" PRId64 "\n", matched,
               k_uptime_get() - discovery_start);
      }
    }
  }

  if (received_count != expected_receive_count() || received_invalid_value) {
    printf("ROS2_ZEPHYR_ERROR operation=receive_timeout value=%" PRIu32 " count=%u invalid=%d\n",
           received_value, received_count, received_invalid_value);
    goto cleanup;
  }
  result = 0;

cleanup:
  if (executor_initialized && rclc_executor_fini(&executor) != RCL_RET_OK) {
    result = 1;
  }
  if (rcl_subscription_fini(&subscription, node) != RCL_RET_OK) {
    result = 1;
  }
  return result;
}

int main(void)
{
#if defined(ROS2_ZEPHYR_WIFI_ROLE_node)
  const char *role = "node";
#elif defined(ROS2_ZEPHYR_WIFI_ROLE_pub)
  const char *role = "pub";
#elif defined(ROS2_ZEPHYR_WIFI_ROLE_pubsub)
  const char *role = "pubsub";
#else
  const char *role = "sub";
#endif
  printf("ROS2_ZEPHYR_START board=%s role=%s reliability=%s durability=%s depth=%u "
         "rmw=%s middleware=%s\n",
         CONFIG_BOARD_TARGET, role, wifi_reliability_name(), wifi_durability_name(),
         ROS2_ZEPHYR_WIFI_DEPTH, ROS2_ZEPHYR_RMW_NAME, ROS2_ZEPHYR_MIDDLEWARE_NAME);
  printf("ROS2_ZEPHYR_GRAPH_LIMITS local_nodes=%d endpoints_per_node=%d participants=%d "
         "nodes=%d endpoints=%d\n",
         CONFIG_ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES,
         CONFIG_ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE,
         CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS,
         CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES,
         CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS);
  if (!connect_wifi()) {
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
  rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
  rcl_node_t node = rcl_get_zero_initialized_node();
  bool init_options_initialized = false;
  bool support_initialized = false;
  bool node_initialized = false;
  int result = 1;

  if (!check(rcl_init_options_init(&init_options, allocator), "init_options_init")) {
    goto cleanup;
  }
  init_options_initialized = true;
  if (!check(rcl_init_options_set_domain_id(&init_options, CONFIG_ROS2_ZEPHYR_DOMAIN_ID),
             "init_options_set_domain_id")) {
    goto cleanup;
  }
  if (!check(rclc_support_init_with_options(&support, 0, NULL, &init_options, &allocator),
             "support_init")) {
    goto cleanup;
  }
  support_initialized = true;
  if (!check(rcl_init_options_fini(&init_options), "init_options_fini")) {
    goto cleanup;
  }
  init_options_initialized = false;
  if (!check(rclc_node_init_default(&node, "ros2_zephyr_esp32s3", "", &support), "node_init")) {
    goto cleanup;
  }
  node_initialized = true;

#if defined(ROS2_ZEPHYR_WIFI_ROLE_node)
  result = run_graph_node(&node, &allocator);
#elif defined(ROS2_ZEPHYR_WIFI_ROLE_pub)
  result = run_publisher(&node);
#elif defined(ROS2_ZEPHYR_WIFI_ROLE_pubsub)
  result = run_graph_pubsub(&node);
#else
  result = run_subscriber(&node, &support, &allocator);
#endif
  report_resources();

cleanup:
  if (node_initialized && rcl_node_fini(&node) != RCL_RET_OK) {
    result = 1;
  }
  if (support_initialized && rclc_support_fini(&support) != RCL_RET_OK) {
    result = 1;
  }
  if (init_options_initialized && rcl_init_options_fini(&init_options) != RCL_RET_OK) {
    result = 1;
  }
  if (rcutils_logging_shutdown() != RCUTILS_RET_OK) {
    result = 1;
  }
  const ros2_zephyr_allocation_metrics_t ros_snapshot =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  const ros2_zephyr_allocation_metrics_t middleware_snapshot =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
  printf("ROS2_ZEPHYR_CLEANUP status=%d ros_live=%zu middleware_live=%zu middleware=%s\n",
         result, ros_snapshot.live_bytes, middleware_snapshot.live_bytes,
         ROS2_ZEPHYR_MIDDLEWARE_NAME);
  return result;
}
