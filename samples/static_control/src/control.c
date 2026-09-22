// SPDX-License-Identifier: Apache-2.0

#include "control.h"

#include <math.h>
#include <stddef.h>

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
  k_timer_init(&release_timer, NULL, NULL);
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
