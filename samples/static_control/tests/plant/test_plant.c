// SPDX-License-Identifier: Apache-2.0

#include <zephyr/ztest.h>

#include "plant.h"

ZTEST(static_control_plant, test_output_is_disabled_at_start)
{
  struct static_control_plant plant;
  struct static_control_plant_output output;
  static_control_plant_init(&plant);

  static_control_plant_step(&plant, 1.0F, 0.01F, false, &output);

  zassert_equal(output.effort, 0.0F);
  zassert_equal(output.position, 0.0F);
  zassert_equal(output.velocity, 0.0F);
}

ZTEST(static_control_plant, test_valid_command_moves_plant)
{
  struct static_control_plant plant;
  struct static_control_plant_output output;
  static_control_plant_init(&plant);

  for (unsigned int cycle = 0U; cycle < 100U; ++cycle) {
    static_control_plant_step(&plant, 1.0F, 0.01F, true, &output);
  }

  zassert_true(output.position > 0.0F);
  zassert_true(output.velocity > 0.0F);
  zassert_true(output.effort >= -1.0F && output.effort <= 1.0F);
}

ZTEST(static_control_plant, test_disabled_output_damps_motion)
{
  struct static_control_plant plant = {.position = 0.5F, .velocity = 1.0F};
  struct static_control_plant_output output;

  static_control_plant_step(&plant, -1.0F, 0.01F, false, &output);

  zassert_equal(output.effort, 0.0F);
  zassert_true(output.velocity < 1.0F);
}

ZTEST_SUITE(static_control_plant, NULL, NULL, NULL, NULL, NULL);
