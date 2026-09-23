// SPDX-License-Identifier: Apache-2.0

#ifndef ROS2_ZEPHYR_STATIC_CONTROL_CONTROL_H_
#define ROS2_ZEPHYR_STATIC_CONTROL_CONTROL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum static_control_status {
  STATIC_CONTROL_DISABLED = 0,
  STATIC_CONTROL_ACTIVE = 1,
  STATIC_CONTROL_STALE = 2,
  STATIC_CONTROL_FAULT = 3,
};

struct static_control_command {
  float target;
  uint32_t sequence;
};

struct static_control_state {
  float position;
  float velocity;
  float effort;
  uint32_t command_sequence;
  uint32_t control_cycles;
  uint32_t skipped_releases;
  uint32_t max_release_lateness_us;
  uint8_t status;
};

bool static_control_start(void);
void static_control_stop(void);
bool static_control_submit(const struct static_control_command *command, int64_t receipt_ms);
struct static_control_state static_control_read_state(void);
bool static_control_stack_space(size_t *unused_bytes);

#ifdef STATIC_CONTROL_TEST
void static_control_test_delay_next_cycle(uint32_t delay_ms);
#endif

#endif
