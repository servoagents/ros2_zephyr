// SPDX-License-Identifier: Apache-2.0
// Print per-thread unused stack space and heap high-water marks.
#include "report.h"

#include <ros2_zephyr/allocator.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>

extern struct k_heap _system_heap;
int malloc_runtime_stats_get(struct sys_memory_stats *stats);

static void report_thread(const struct k_thread *thread, void *phase)
{
  size_t unused = 0;
  const char *name = k_thread_name_get((k_tid_t)thread);
  int rc = k_thread_stack_space_get(thread, &unused);

  printk("C6_STACK phase=%s name=%s size=%u unused=%u rc=%d\n", (const char *)phase,
         (name != NULL && name[0] != '\0') ? name : "unnamed",
         (unsigned)thread->stack_info.size, (unsigned)unused, rc);
}

void app_resource_report(const char *phase)
{
  struct sys_memory_stats kernel = {0};
  struct sys_memory_stats libc = {0};
  k_spinlock_key_t key = k_spin_lock(&_system_heap.lock);
  int kernel_rc = sys_heap_runtime_stats_get(&_system_heap.heap, &kernel);
  k_spin_unlock(&_system_heap.lock, key);
  int libc_rc = malloc_runtime_stats_get(&libc);
  ros2_zephyr_allocation_metrics_t ros =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_ROS);
  ros2_zephyr_allocation_metrics_t middleware =
      ros2_zephyr_allocation_metrics(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);

  printk("C6_HEAP phase=%s kernel_allocated=%u kernel_free=%u kernel_max=%u kernel_rc=%d "
         "libc_allocated=%u libc_free=%u libc_max=%u libc_rc=%d\n",
         phase, (unsigned)kernel.allocated_bytes, (unsigned)kernel.free_bytes,
         (unsigned)kernel.max_allocated_bytes, kernel_rc, (unsigned)libc.allocated_bytes,
         (unsigned)libc.free_bytes, (unsigned)libc.max_allocated_bytes, libc_rc);
  printk("C6_ALLOC phase=%s ros_live=%u ros_high=%u middleware_live=%u middleware_high=%u\n",
         phase, (unsigned)ros.live_bytes, (unsigned)ros.high_water_bytes,
         (unsigned)middleware.live_bytes, (unsigned)middleware.high_water_bytes);
  k_thread_foreach_unlocked(report_thread, (void *)phase);
  printk("C6_REPORT_END phase=%s uptime_ms=%lld\n", phase, k_uptime_get());
}
