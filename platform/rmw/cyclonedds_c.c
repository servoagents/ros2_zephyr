// SPDX-License-Identifier: Apache-2.0

#include <dds/ddsrt/heap.h>
#include <ros2_zephyr/allocator.h>
#include <zephyr/init.h>

static void *middleware_allocate(size_t size)
{
  return ros2_zephyr_allocate(
      size, ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE));
}

static void *middleware_zero_allocate(size_t count, size_t size)
{
  return ros2_zephyr_zero_allocate(
      count, size, ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE));
}

static void *middleware_reallocate(void *pointer, size_t size)
{
  return ros2_zephyr_reallocate(
      pointer, size, ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE));
}

static void middleware_deallocate(void *pointer)
{
  ros2_zephyr_deallocate(
      pointer, ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE));
}

static int cyclonedds_allocator_init(void)
{
  const ddsrt_allocation_ops_t allocator = {
      .malloc = middleware_allocate,
      .calloc = middleware_zero_allocate,
      .realloc = middleware_reallocate,
      .free = middleware_deallocate,
  };
  ddsrt_set_allocator(allocator);
  return 0;
}

SYS_INIT(cyclonedds_allocator_init, APPLICATION, 0);
