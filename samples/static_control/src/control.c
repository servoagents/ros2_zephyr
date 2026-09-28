// SPDX-License-Identifier: Apache-2.0

#include "control.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "plant.h"

enum {
  CONTROL_PERIOD_MS = 10,
  CONTROL_DEADLINE_MS = 10,
  COMMAND_EXPIRY_MS = 250,
  CONTROL_THREAD_PRIORITY = 3,
  CONTROL_STACK_SIZE = 2048,
};

BUILD_ASSERT(CONFIG_MAIN_THREAD_PRIORITY > CONTROL_THREAD_PRIORITY,
             "the ROS-owning main thread must not preempt the control thread");

struct command_snapshot {
  struct k_spinlock lock;
  struct static_control_command value;
  int64_t receipt_ms;
  bool valid;
};

struct state_snapshot {
  struct k_spinlock lock;
  struct static_control_state value;
};

static struct command_snapshot command_snapshot;
static struct state_snapshot state_snapshot;
static struct k_timer release_timer;
static atomic_t stop_requested;
static atomic_t running;
#ifdef STATIC_CONTROL_TEST
static atomic_t test_delay_ms;
#endif
K_THREAD_STACK_DEFINE(control_stack, CONTROL_STACK_SIZE);
static struct k_thread control_thread;
static k_tid_t control_thread_id;

#ifdef STATIC_CONTROL_TIMING_PROBE
enum { TIMING_BUCKET_US = 100, TIMING_BUCKET_COUNT = 102 };
static struct {
  struct k_spinlock lock;
  uint32_t last_release_cycles;
  uint32_t samples;
  uint32_t release_hist[TIMING_BUCKET_COUNT];
  uint32_t completion_hist[TIMING_BUCKET_COUNT];
  uint32_t release_max_us;
  uint32_t completion_max_us;
} timing;

static void release_expiry(struct k_timer *timer)
{
  ARG_UNUSED(timer);
  const uint32_t cycles = k_cycle_get_32();
  k_spinlock_key_t key = k_spin_lock(&timing.lock);
  timing.last_release_cycles = cycles;
  k_spin_unlock(&timing.lock, key);
}

static uint32_t timing_release_cycles(void)
{
  k_spinlock_key_t key = k_spin_lock(&timing.lock);
  const uint32_t cycles = timing.last_release_cycles;
  k_spin_unlock(&timing.lock, key);
  return cycles;
}

static void timing_record(uint32_t release_cycles, uint32_t start_cycles,
                          uint32_t completion_cycles)
{
  /* Each difference spans at most one 10 ms period, well below 2^32 cycles.
   * Unsigned subtraction remains correct across a counter wrap.
   */
  const uint64_t release_us_64 =
      k_cyc_to_us_floor64((uint32_t)(start_cycles - release_cycles));
  const uint64_t completion_us_64 =
      k_cyc_to_us_floor64((uint32_t)(completion_cycles - release_cycles));
  const uint32_t release_us = (uint32_t)MIN(release_us_64, UINT32_MAX);
  const uint32_t completion_late_us =
      (uint32_t)MIN(completion_us_64 > CONTROL_DEADLINE_MS * 1000U
                        ? completion_us_64 - CONTROL_DEADLINE_MS * 1000U : 0U,
                    UINT32_MAX);
  const size_t release_bucket = MIN(release_us / TIMING_BUCKET_US,
                                    TIMING_BUCKET_COUNT - 1U);
  const size_t completion_bucket = MIN(completion_late_us / TIMING_BUCKET_US,
                                       TIMING_BUCKET_COUNT - 1U);
  k_spinlock_key_t key = k_spin_lock(&timing.lock);
  timing.samples++;
  timing.release_hist[release_bucket]++;
  timing.completion_hist[completion_bucket]++;
  timing.release_max_us = MAX(timing.release_max_us, release_us);
  timing.completion_max_us = MAX(timing.completion_max_us, completion_late_us);
  k_spin_unlock(&timing.lock, key);
}

static uint32_t timing_percentile(const uint32_t *hist, uint32_t samples, uint32_t percentile)
{
  if (samples == 0U) {
    return 0U;
  }
  const uint32_t rank = (samples * percentile + 99U) / 100U;
  uint32_t cumulative = 0U;
  for (size_t bucket = 0; bucket < TIMING_BUCKET_COUNT; bucket++) {
    cumulative += hist[bucket];
    if (cumulative >= rank) {
      return (uint32_t)(bucket + 1U) * TIMING_BUCKET_US;
    }
  }
  return TIMING_BUCKET_COUNT * TIMING_BUCKET_US;
}

struct static_control_timing static_control_timing_read(void)
{
  uint32_t release_hist[TIMING_BUCKET_COUNT];
  uint32_t completion_hist[TIMING_BUCKET_COUNT];
  struct static_control_timing result;
  k_spinlock_key_t key = k_spin_lock(&timing.lock);
  result.samples = timing.samples;
  result.release_max_us = timing.release_max_us;
  result.completion_max_us = timing.completion_max_us;
  memcpy(release_hist, timing.release_hist, sizeof(release_hist));
  memcpy(completion_hist, timing.completion_hist, sizeof(completion_hist));
  k_spin_unlock(&timing.lock, key);
  result.release_p50_us = timing_percentile(release_hist, result.samples, 50U);
  result.release_p95_us = timing_percentile(release_hist, result.samples, 95U);
  result.release_p99_us = timing_percentile(release_hist, result.samples, 99U);
  result.completion_p50_us = timing_percentile(completion_hist, result.samples, 50U);
  result.completion_p95_us = timing_percentile(completion_hist, result.samples, 95U);
  result.completion_p99_us = timing_percentile(completion_hist, result.samples, 99U);
  return result;
}
#endif

static void publish_state(const struct static_control_state *state)
{
  k_spinlock_key_t key = k_spin_lock(&state_snapshot.lock);
  state_snapshot.value = *state;
  k_spin_unlock(&state_snapshot.lock, key);
}

static void read_command(struct static_control_command *command, int64_t *receipt_ms, bool *valid)
{
  k_spinlock_key_t key = k_spin_lock(&command_snapshot.lock);
  *command = command_snapshot.value;
  *receipt_ms = command_snapshot.receipt_ms;
  *valid = command_snapshot.valid;
  k_spin_unlock(&command_snapshot.lock, key);
}

static void control_entry(void *first, void *second, void *third)
{
  ARG_UNUSED(first);
  ARG_UNUSED(second);
  ARG_UNUSED(third);

  struct static_control_plant plant;
  struct static_control_state state = {.status = STATIC_CONTROL_DISABLED};
  int64_t scheduled_release_ms = k_uptime_get() + CONTROL_PERIOD_MS;
  bool faulted = false;
  static_control_plant_init(&plant);
  publish_state(&state);
  k_timer_start(&release_timer, K_MSEC(CONTROL_PERIOD_MS), K_MSEC(CONTROL_PERIOD_MS));

  while (atomic_get(&stop_requested) == 0) {
    const uint32_t releases = k_timer_status_sync(&release_timer);
    if (atomic_get(&stop_requested) != 0) {
      break;
    }
#ifdef STATIC_CONTROL_TIMING_PROBE
    const uint32_t release_cycles = timing_release_cycles();
    const uint32_t start_cycles = k_cycle_get_32();
#endif

    const int64_t start_ms = k_uptime_get();
#ifdef STATIC_CONTROL_TEST
    const atomic_val_t delay_ms = atomic_set(&test_delay_ms, 0);
    if (delay_ms > 0) {
      k_sleep(K_MSEC(delay_ms));
    }
#endif
    const int64_t lateness_ms = start_ms - scheduled_release_ms;
    if (lateness_ms > 0) {
      state.max_release_lateness_us =
          MAX(state.max_release_lateness_us, (uint32_t)lateness_ms * 1000U);
    }
    if (releases > 1U) {
      state.skipped_releases += releases - 1U;
      faulted = true;
    }
    scheduled_release_ms += (int64_t)releases * CONTROL_PERIOD_MS;

    struct static_control_command command = {0};
    int64_t receipt_ms = 0;
    bool command_valid = false;
    read_command(&command, &receipt_ms, &command_valid);
    const bool command_fresh =
        command_valid && start_ms >= receipt_ms && start_ms - receipt_ms <= COMMAND_EXPIRY_MS;
    if (start_ms > scheduled_release_ms) {
      faulted = true;
    }

    struct static_control_plant_output output;
    static_control_plant_step(&plant, command.target, CONTROL_PERIOD_MS / 1000.0F,
                              command_fresh && !faulted, &output);
    const int64_t completion_ms = k_uptime_get();
#ifdef STATIC_CONTROL_TIMING_PROBE
    if (releases == 1U) {
      timing_record(release_cycles, start_cycles, k_cycle_get_32());
    }
#endif
    if (completion_ms > scheduled_release_ms - CONTROL_PERIOD_MS + CONTROL_DEADLINE_MS) {
      faulted = true;
      output.effort = 0.0F;
    }

    state.position = output.position;
    state.velocity = output.velocity;
    state.effort = faulted ? 0.0F : output.effort;
    state.command_sequence = command.sequence;
    state.control_cycles++;
    state.status = faulted          ? STATIC_CONTROL_FAULT
                   : !command_valid ? STATIC_CONTROL_DISABLED
                   : !command_fresh ? STATIC_CONTROL_STALE
                                    : STATIC_CONTROL_ACTIVE;
    publish_state(&state);
  }

  k_timer_stop(&release_timer);
  atomic_clear(&running);
}

bool static_control_start(void)
{
  if (!atomic_cas(&running, 0, 1)) {
    return false;
  }
  atomic_clear(&stop_requested);
#ifdef STATIC_CONTROL_TEST
  atomic_clear(&test_delay_ms);
#endif
  command_snapshot.valid = false;
  state_snapshot.value = (struct static_control_state){.status = STATIC_CONTROL_DISABLED};
#ifdef STATIC_CONTROL_TIMING_PROBE
  memset(&timing, 0, sizeof(timing));
  k_timer_init(&release_timer, release_expiry, NULL);
#else
  k_timer_init(&release_timer, NULL, NULL);
#endif
  control_thread_id =
      k_thread_create(&control_thread, control_stack, K_THREAD_STACK_SIZEOF(control_stack),
                      control_entry, NULL, NULL, NULL, CONTROL_THREAD_PRIORITY, 0, K_NO_WAIT);
  k_thread_name_set(control_thread_id, "static-control");
  return true;
}

void static_control_stop(void)
{
  if (atomic_get(&running) == 0) {
    return;
  }
  atomic_set(&stop_requested, 1);
  k_timer_start(&release_timer, K_TICKS(1), K_NO_WAIT);
  (void)k_thread_join(control_thread_id, K_FOREVER);
}

bool static_control_submit(const struct static_control_command *command, int64_t receipt_ms)
{
  if (command == NULL || !isfinite(command->target) || command->target < -1.0F ||
      command->target > 1.0F || receipt_ms < 0) {
    return false;
  }
  k_spinlock_key_t key = k_spin_lock(&command_snapshot.lock);
  command_snapshot.value = *command;
  command_snapshot.receipt_ms = receipt_ms;
  command_snapshot.valid = true;
  k_spin_unlock(&command_snapshot.lock, key);
  return true;
}

struct static_control_state static_control_read_state(void)
{
  k_spinlock_key_t key = k_spin_lock(&state_snapshot.lock);
  const struct static_control_state state = state_snapshot.value;
  k_spin_unlock(&state_snapshot.lock, key);
  return state;
}

bool static_control_stack_space(size_t *unused_bytes)
{
  return unused_bytes != NULL && control_thread_id != NULL &&
         k_thread_stack_space_get(control_thread_id, unused_bytes) == 0;
}

#ifdef STATIC_CONTROL_TEST
void static_control_test_delay_next_cycle(uint32_t delay_ms)
{
  atomic_set(&test_delay_ms, (atomic_val_t)delay_ms);
}
#endif
