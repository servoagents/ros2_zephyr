// SPDX-License-Identifier: Apache-2.0

#ifndef ROS2_ZEPHYR_STATIC_CONTROL_PLANT_H_
#define ROS2_ZEPHYR_STATIC_CONTROL_PLANT_H_

#include <stdbool.h>

struct static_control_plant {
  float position;
  float velocity;
};

struct static_control_plant_output {
  float position;
  float velocity;
  float effort;
};

void static_control_plant_init(struct static_control_plant *plant);

void static_control_plant_step(struct static_control_plant *plant, float target,
                               float period_seconds, bool output_enabled,
                               struct static_control_plant_output *output);

#endif
