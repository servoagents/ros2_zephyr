// SPDX-License-Identifier: Apache-2.0

#include <math.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "control.h"

static void start_control(void *fixture)
{
  ARG_UNUSED(fixture);
  zassert_true(static_control_start());
}

static void stop_control(void *fixture)
{
  ARG_UNUSED(fixture);
  static_control_stop();
}

ZTEST_SUITE(static_control_policy, NULL, NULL, start_control, stop_control, NULL);

ZTEST(static_control_policy, test_command_burst_and_expiry)
{
  for (uint32_t sequence = 1U; sequence <= 1000U; ++sequence) {
    const struct static_control_command command = {
        .target = 0.5F,
        .sequence = sequence,
    };
    zassert_true(static_control_submit(&command, k_uptime_get()));
  }

  k_sleep(K_MSEC(40));
  struct static_control_state state = static_control_read_state();
  zassert_equal(state.status, STATIC_CONTROL_ACTIVE);
  zassert_equal(state.command_sequence, 1000U);
  zassert_true(state.control_cycles >= 2U);

  k_sleep(K_MSEC(280));
  state = static_control_read_state();
  zassert_equal(state.status, STATIC_CONTROL_STALE);
  zassert_within(state.effort, 0.0F, 0.0001F);
}

ZTEST(static_control_policy, test_invalid_commands_are_rejected)
{
  const struct static_control_command too_large = {.target = 1.01F, .sequence = 1U};
  const struct static_control_command not_finite = {.target = NAN, .sequence = 2U};

  zassert_false(static_control_submit(NULL, k_uptime_get()));
  zassert_false(static_control_submit(&too_large, k_uptime_get()));
  zassert_false(static_control_submit(&not_finite, k_uptime_get()));
  zassert_false(static_control_submit(&too_large, -1));
}

ZTEST(static_control_policy, test_overrun_latches_fault)
{
  const struct static_control_command command = {.target = 0.5F, .sequence = 3U};
  zassert_true(static_control_submit(&command, k_uptime_get()));
  k_sleep(K_MSEC(20));

  static_control_test_delay_next_cycle(30U);
  k_sleep(K_MSEC(70));
  const struct static_control_state state = static_control_read_state();
  zassert_equal(state.status, STATIC_CONTROL_FAULT);
  zassert_true(state.skipped_releases > 0U);
  zassert_within(state.effort, 0.0F, 0.0001F);
}
