// SPDX-License-Identifier: Apache-2.0

#include <ros2_zephyr/allocator.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#if defined(CONFIG_ESP_SPIRAM)
#include <zephyr/multi_heap/shared_multi_heap.h>
#endif
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/util.h>

enum {
  ALLOCATION_POOL_INTERNAL = 0U,
  ALLOCATION_POOL_EXTERNAL = 1U,
  ROS_EXTERNAL_ALLOCATION_MIN = 4096U,
  MIDDLEWARE_EXTERNAL_ALLOCATION_MIN = 8192U,
};

typedef struct allocation_header_s {
  size_t size;
  uint32_t pool;
} allocation_header_t;

typedef struct allocation_state_s {
  ros2_zephyr_allocation_domain_t domain;
  size_t external_threshold;
  ros2_zephyr_allocation_metrics_t metrics;
} allocation_state_t;

static allocation_state_t allocation_states[] = {
    {ROS2_ZEPHYR_ALLOCATION_ROS, ROS_EXTERNAL_ALLOCATION_MIN, {0}},
    {ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE, MIDDLEWARE_EXTERNAL_ALLOCATION_MIN, {0}},
};
static struct k_spinlock allocation_metrics_lock;
#if defined(CONFIG_ROS2_ZEPHYR_TEST_ROS_ALLOCATION_FAILURE_AT) && \
    CONFIG_ROS2_ZEPHYR_TEST_ROS_ALLOCATION_FAILURE_AT > 0
static size_t ros_allocation_attempts;
#endif
#if defined(CONFIG_ESP_SPIRAM)
static K_MUTEX_DEFINE(external_heap_mutex);
#endif

static allocation_state_t *state_for_domain(ros2_zephyr_allocation_domain_t domain)
{
  return domain == ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE ? &allocation_states[1]
                                                     : &allocation_states[0];
}

void *ros2_zephyr_allocator_state(ros2_zephyr_allocation_domain_t domain)
{
  return state_for_domain(domain);
}

static void metrics_add(allocation_state_t *state, size_t size)
{
  k_spinlock_key_t key = k_spin_lock(&allocation_metrics_lock);
  state->metrics.calls++;
  state->metrics.live_bytes += size;
  if (state->metrics.live_bytes > state->metrics.high_water_bytes) {
    state->metrics.high_water_bytes = state->metrics.live_bytes;
  }
  k_spin_unlock(&allocation_metrics_lock, key);
}

ros2_zephyr_allocation_metrics_t ros2_zephyr_allocation_metrics(
    ros2_zephyr_allocation_domain_t domain)
{
  allocation_state_t *state = state_for_domain(domain);
  k_spinlock_key_t key = k_spin_lock(&allocation_metrics_lock);
  ros2_zephyr_allocation_metrics_t snapshot = state->metrics;
  k_spin_unlock(&allocation_metrics_lock, key);
  return snapshot;
}

static void report_allocation_failure(allocation_state_t *state, size_t requested)
{
  const ros2_zephyr_allocation_metrics_t snapshot =
      ros2_zephyr_allocation_metrics(state->domain);
  const char *domain = state->domain == ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE ? "middleware" : "ros";

  printf("ROS2_ZEPHYR_ALLOC_FAILURE domain=%s requested=%zu live=%zu high_water=%zu\n", domain,
         requested, snapshot.live_bytes, snapshot.high_water_bytes);
#if defined(CONFIG_SYS_HEAP_RUNTIME_STATS) && defined(CONFIG_SYS_HEAP_ARRAY_SIZE)
  struct sys_heap **heaps = NULL;
  const int heap_count = sys_heap_array_get(&heaps);
  for (int index = 0; index < heap_count; ++index) {
    struct sys_memory_stats stats;
    if (sys_heap_runtime_stats_get(heaps[index], &stats) == 0) {
      printf("ROS2_ZEPHYR_ALLOC_FAILURE_HEAP index=%d allocated=%zu free=%zu peak=%zu\n", index,
             stats.allocated_bytes, stats.free_bytes, stats.max_allocated_bytes);
    }
  }
#endif
}

static bool inject_allocation_failure(allocation_state_t *state)
{
#if defined(CONFIG_ROS2_ZEPHYR_TEST_ROS_ALLOCATION_FAILURE_AT) && \
    CONFIG_ROS2_ZEPHYR_TEST_ROS_ALLOCATION_FAILURE_AT > 0
  if (state->domain == ROS2_ZEPHYR_ALLOCATION_ROS) {
    k_spinlock_key_t key = k_spin_lock(&allocation_metrics_lock);
    const size_t attempt = ++ros_allocation_attempts;
    k_spin_unlock(&allocation_metrics_lock, key);
    return attempt == CONFIG_ROS2_ZEPHYR_TEST_ROS_ALLOCATION_FAILURE_AT;
  }
#else
  (void)state;
#endif
  return false;
}

void *ros2_zephyr_allocate(size_t size, void *opaque_state)
{
  allocation_state_t *state = opaque_state;
  if (inject_allocation_failure(state)) {
    report_allocation_failure(state, size);
    return NULL;
  }
  allocation_header_t *header = NULL;
#if defined(CONFIG_ESP_SPIRAM)
  const bool external = size >= state->external_threshold;
  if (external) {
    k_mutex_lock(&external_heap_mutex, K_FOREVER);
    header = shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, sizeof(*header) + size);
    k_mutex_unlock(&external_heap_mutex);
  } else
#endif
  {
    header = malloc(sizeof(*header) + size);
  }
  if (header == NULL) {
    report_allocation_failure(state, size);
    return NULL;
  }
  header->size = size;
#if defined(CONFIG_ESP_SPIRAM)
  header->pool = external ? ALLOCATION_POOL_EXTERNAL : ALLOCATION_POOL_INTERNAL;
#else
  header->pool = ALLOCATION_POOL_INTERNAL;
#endif
  metrics_add(state, size);
  return header + 1;
}

void ros2_zephyr_deallocate(void *pointer, void *opaque_state)
{
  if (pointer == NULL) {
    return;
  }
  allocation_state_t *state = opaque_state;
  allocation_header_t *header = (allocation_header_t *)pointer - 1;
  k_spinlock_key_t key = k_spin_lock(&allocation_metrics_lock);
  state->metrics.frees++;
  state->metrics.live_bytes -= header->size;
  k_spin_unlock(&allocation_metrics_lock, key);
#if defined(CONFIG_ESP_SPIRAM)
  if (header->pool == ALLOCATION_POOL_EXTERNAL) {
    k_mutex_lock(&external_heap_mutex, K_FOREVER);
    shared_multi_heap_free(header);
    k_mutex_unlock(&external_heap_mutex);
  } else
#endif
  {
    free(header);
  }
}

void *ros2_zephyr_reallocate(void *pointer, size_t size, void *opaque_state)
{
  if (pointer == NULL) {
    return ros2_zephyr_allocate(size, opaque_state);
  }
  allocation_state_t *state = opaque_state;
  if (inject_allocation_failure(state)) {
    report_allocation_failure(state, size);
    return NULL;
  }
  allocation_header_t *old_header = (allocation_header_t *)pointer - 1;
  const size_t old_size = old_header->size;
  const uint32_t old_pool = old_header->pool;
#if defined(CONFIG_ESP_SPIRAM)
  const uint32_t new_pool = size >= state->external_threshold ? ALLOCATION_POOL_EXTERNAL
                                                              : ALLOCATION_POOL_INTERNAL;
#else
  const uint32_t new_pool = ALLOCATION_POOL_INTERNAL;
#endif
  allocation_header_t *new_header = NULL;
#if defined(CONFIG_ESP_SPIRAM)
  if (old_pool == new_pool && new_pool == ALLOCATION_POOL_EXTERNAL) {
    k_mutex_lock(&external_heap_mutex, K_FOREVER);
    new_header = shared_multi_heap_realloc(SMH_REG_ATTR_EXTERNAL, old_header,
                                           sizeof(*new_header) + size);
    k_mutex_unlock(&external_heap_mutex);
  } else if (old_pool != new_pool && new_pool == ALLOCATION_POOL_EXTERNAL) {
    k_mutex_lock(&external_heap_mutex, K_FOREVER);
    new_header = shared_multi_heap_alloc(SMH_REG_ATTR_EXTERNAL, sizeof(*new_header) + size);
    k_mutex_unlock(&external_heap_mutex);
  } else
#endif
  {
    new_header = old_pool == new_pool ? realloc(old_header, sizeof(*new_header) + size)
                                      : malloc(sizeof(*new_header) + size);
  }
  if (new_header == NULL) {
    report_allocation_failure(state, size);
    return NULL;
  }
  if (old_pool != new_pool) {
    memcpy(new_header + 1, pointer, MIN(old_size, size));
#if defined(CONFIG_ESP_SPIRAM)
    if (old_pool == ALLOCATION_POOL_EXTERNAL) {
      k_mutex_lock(&external_heap_mutex, K_FOREVER);
      shared_multi_heap_free(old_header);
      k_mutex_unlock(&external_heap_mutex);
    } else
#endif
    {
      free(old_header);
    }
  }
  k_spinlock_key_t key = k_spin_lock(&allocation_metrics_lock);
  state->metrics.calls++;
  state->metrics.live_bytes -= old_size;
  state->metrics.live_bytes += size;
  if (state->metrics.live_bytes > state->metrics.high_water_bytes) {
    state->metrics.high_water_bytes = state->metrics.live_bytes;
  }
  k_spin_unlock(&allocation_metrics_lock, key);
  new_header->size = size;
  new_header->pool = new_pool;
  return new_header + 1;
}

void *ros2_zephyr_zero_allocate(size_t count, size_t size, void *state)
{
  if (size != 0U && count > SIZE_MAX / size) {
    return NULL;
  }
  const size_t bytes = count * size;
  void *pointer = ros2_zephyr_allocate(bytes, state);
  if (pointer != NULL) {
    memset(pointer, 0, bytes);
  }
  return pointer;
}
