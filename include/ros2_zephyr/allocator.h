// SPDX-License-Identifier: Apache-2.0

#ifndef ROS2_ZEPHYR__ALLOCATOR_H_
#define ROS2_ZEPHYR__ALLOCATOR_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ros2_zephyr_allocation_domain_e {
  ROS2_ZEPHYR_ALLOCATION_ROS = 0,
  ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE = 1,
} ros2_zephyr_allocation_domain_t;

typedef struct ros2_zephyr_allocation_metrics_s {
  size_t calls;
  size_t frees;
  size_t live_bytes;
  size_t high_water_bytes;
} ros2_zephyr_allocation_metrics_t;

void *ros2_zephyr_allocator_state(ros2_zephyr_allocation_domain_t domain);
void *ros2_zephyr_allocate(size_t size, void *state);
void ros2_zephyr_deallocate(void *pointer, void *state);
void *ros2_zephyr_reallocate(void *pointer, size_t size, void *state);
void *ros2_zephyr_zero_allocate(size_t count, size_t size, void *state);
ros2_zephyr_allocation_metrics_t ros2_zephyr_allocation_metrics(
    ros2_zephyr_allocation_domain_t domain);

#ifdef __cplusplus
}
#endif

#endif  // ROS2_ZEPHYR__ALLOCATOR_H_
