// SPDX-License-Identifier: Apache-2.0

#include "plant.h"

enum {
  PROPORTIONAL_GAIN = 4,
  DAMPING_GAIN = 1,
};

static float clamp(float value, float lower, float upper)
{
  if (value < lower) {
    return lower;
  }
  if (value > upper) {
    return upper;
  }
  return value;
}

void static_control_plant_init(struct static_control_plant *plant)
{
  plant->position = 0.0F;
  plant->velocity = 0.0F;
}

void static_control_plant_step(struct static_control_plant *plant, float target,
                               float period_seconds, bool output_enabled,
                               struct static_control_plant_output *output)
{
  float effort = 0.0F;
  if (output_enabled) {
    effort = clamp(PROPORTIONAL_GAIN * (target - plant->position) - DAMPING_GAIN * plant->velocity,
                   -1.0F, 1.0F);
  }

  const float acceleration = effort - 0.2F * plant->velocity;
  plant->velocity += acceleration * period_seconds;
  plant->position += plant->velocity * period_seconds;

  output->position = plant->position;
  output->velocity = plant->velocity;
  output->effort = effort;
}
