// SPDX-License-Identifier: Apache-2.0

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#include "tls_external.h"

#define HANDOFFS 2000U
#define MAIN_VALUE 0x33333333U
#define INITIALIZED_VALUE 0xa5a5a5a5U
#define EXTERNAL_INITIALIZED_VALUE 0x5a5a5a5aU
#define ALIGNED_INITIALIZED_VALUE 0x12345678U

static __thread volatile uint32_t thread_value;
static __thread volatile uint32_t initialized_value = INITIALIZED_VALUE;
static __thread volatile uint32_t zero_value;
static __thread volatile uint32_t aligned_value __attribute__((aligned(16))) =
    ALIGNED_INITIALIZED_VALUE;
static struct k_sem ready;
static struct k_sem done;
static struct k_sem turn[2];
static struct k_thread native_workers[2];
K_THREAD_STACK_ARRAY_DEFINE(native_stacks, 2, 4096);

struct observation {
  uintptr_t address;
  uintptr_t external_address;
  uintptr_t aligned_address;
  uint32_t first;
  uint32_t last;
  uint32_t initialized_first;
  uint32_t zero_first;
  uint32_t external_first;
  uint32_t aligned_first;
  uint32_t mismatches;
  uint32_t timeouts;
};

static struct observation observed[2];

static uint32_t expected_value(uintptr_t index) { return index == 0 ? 0x11111111U : 0x22222222U; }

static bool values_match(uint32_t expected)
{
  return thread_value == expected && initialized_value == expected && zero_value == expected &&
         aligned_value == expected && tls_external_read() == expected;
}

/* Both thread APIs execute this exact body and touch the same TLS variables. */
static void *worker(void *argument)
{
  uintptr_t index = (uintptr_t)argument;
  const uint32_t expected = expected_value(index);
  observed[index].initialized_first = initialized_value;
  observed[index].zero_first = zero_value;
  observed[index].external_first = tls_external_read();
  observed[index].aligned_first = aligned_value;
  thread_value = expected;
  initialized_value = expected;
  zero_value = expected;
  aligned_value = expected;
  tls_external_write(expected);
  observed[index].address = (uintptr_t)&thread_value;
  observed[index].external_address = tls_external_address();
  observed[index].aligned_address = (uintptr_t)&aligned_value;
  observed[index].first = thread_value;
  k_sem_give(&ready);

  for (unsigned int step = 0; step < HANDOFFS; step++) {
    if (k_sem_take(&turn[index], K_SECONDS(2)) != 0) {
      observed[index].timeouts++;
      break;
    }
    if (!values_match(expected))
      observed[index].mismatches++;
    k_yield();
    if (!values_match(expected))
      observed[index].mismatches++;
    k_sem_give(&turn[1U - index]);
  }

  observed[index].last = thread_value;
  k_sem_give(&done);
  return NULL;
}

static void native_worker(void *argument, void *unused1, void *unused2)
{
  ARG_UNUSED(unused1);
  ARG_UNUSED(unused2);
  (void)worker(argument);
}

static bool run_case(bool pthread_mode, unsigned int round)
{
  pthread_t posix_workers[2];
  pthread_attr_t posix_attrs[2];
  const char *mode = pthread_mode ? "pthread" : "native";
  unsigned int created = 0;
  bool created_all = true;

  /* Allocate emulated TLS before creating workers, so an allocator race
     cannot accidentally produce distinct worker addresses. */
  thread_value = MAIN_VALUE;
  initialized_value = MAIN_VALUE;
  zero_value = MAIN_VALUE;
  aligned_value = MAIN_VALUE;
  tls_external_write(MAIN_VALUE);
  uintptr_t main_address = (uintptr_t)&thread_value;
  uintptr_t main_external_address = tls_external_address();
  uintptr_t main_aligned_address = (uintptr_t)&aligned_value;
  k_sem_init(&ready, 0, 2);
  k_sem_init(&done, 0, 2);
  for (unsigned int index = 0; index < 2; index++) {
    observed[index] = (struct observation){0};
    k_sem_init(&turn[index], 0, 1);
  }

  for (uintptr_t index = 0; index < 2; index++) {
    if (pthread_mode) {
      /* Zephyr's pthread port requires an explicit stack when dynamic
         thread allocation is disabled. Native workers have already joined. */
      int result = pthread_attr_init(&posix_attrs[index]);
      bool attr_initialized = result == 0;
      if (result == 0)
        result = pthread_attr_setstack(&posix_attrs[index], native_stacks[index],
                                       K_THREAD_STACK_SIZEOF(native_stacks[index]));
      if (result == 0)
        result = pthread_create(&posix_workers[index], &posix_attrs[index], worker, (void *)index);
      if (result != 0) {
        printk("THREAD_TLS_CREATE_FAIL mode=%s code=%d\n", mode, result);
        if (attr_initialized)
          (void)pthread_attr_destroy(&posix_attrs[index]);
        created_all = false;
        break;
      }
    } else {
      k_thread_create(&native_workers[index], native_stacks[index],
                      K_THREAD_STACK_SIZEOF(native_stacks[index]), native_worker, (void *)index,
                      NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
    }
    created++;
  }

  bool ready_twice =
      created_all && k_sem_take(&ready, K_SECONDS(5)) == 0 && k_sem_take(&ready, K_SECONDS(5)) == 0;
  k_sem_give(&turn[0]);
  bool finished = true;
  for (unsigned int index = 0; index < created; index++)
    finished &= k_sem_take(&done, K_SECONDS(10)) == 0;

  bool joined = true;
  for (unsigned int index = 0; index < created; index++) {
    if (pthread_mode) {
      joined &= pthread_join(posix_workers[index], NULL) == 0;
      joined &= pthread_attr_destroy(&posix_attrs[index]) == 0;
    } else {
      joined &= k_thread_join(&native_workers[index], K_SECONDS(2)) == 0;
    }
  }

  bool isolated =
      created_all && ready_twice && finished && joined && main_address != observed[0].address &&
      main_address != observed[1].address && observed[0].address != observed[1].address &&
      main_external_address != observed[0].external_address &&
      main_external_address != observed[1].external_address &&
      observed[0].external_address != observed[1].external_address &&
      main_aligned_address != observed[0].aligned_address &&
      main_aligned_address != observed[1].aligned_address &&
      observed[0].aligned_address != observed[1].aligned_address &&
      (main_aligned_address & 15U) == 0U && (observed[0].aligned_address & 15U) == 0U &&
      (observed[1].aligned_address & 15U) == 0U &&
      observed[0].initialized_first == INITIALIZED_VALUE &&
      observed[1].initialized_first == INITIALIZED_VALUE && observed[0].zero_first == 0U &&
      observed[1].zero_first == 0U && observed[0].external_first == EXTERNAL_INITIALIZED_VALUE &&
      observed[1].external_first == EXTERNAL_INITIALIZED_VALUE &&
      observed[0].aligned_first == ALIGNED_INITIALIZED_VALUE &&
      observed[1].aligned_first == ALIGNED_INITIALIZED_VALUE &&
      observed[0].first == expected_value(0) && observed[1].first == expected_value(1) &&
      observed[0].last == expected_value(0) && observed[1].last == expected_value(1) &&
      observed[0].mismatches == 0 && observed[1].mismatches == 0 && observed[0].timeouts == 0 &&
      observed[1].timeouts == 0 && values_match(MAIN_VALUE);
  printk("THREAD_TLS_CASE mode=%s round=%u result=%s main=%p a=%p b=%p "
         "external=%p/%p/%p aligned=%p/%p/%p "
         "initial=%x/%x zero=%x/%x other=%x/%x align_initial=%x/%x "
         "first=%x/%x last=%x/%x mismatches=%u/%u timeouts=%u/%u handoffs=%u\n",
         mode, round, isolated ? "PASS" : "FAIL", (void *)main_address, (void *)observed[0].address,
         (void *)observed[1].address, (void *)main_external_address,
         (void *)observed[0].external_address, (void *)observed[1].external_address,
         (void *)main_aligned_address, (void *)observed[0].aligned_address,
         (void *)observed[1].aligned_address, observed[0].initialized_first,
         observed[1].initialized_first, observed[0].zero_first, observed[1].zero_first,
         observed[0].external_first, observed[1].external_first, observed[0].aligned_first,
         observed[1].aligned_first, observed[0].first, observed[1].first, observed[0].last,
         observed[1].last, observed[0].mismatches, observed[1].mismatches, observed[0].timeouts,
         observed[1].timeouts, HANDOFFS);
  return isolated;
}

int main(void)
{
  bool native_ok = run_case(false, 0);
  native_ok = run_case(false, 1) && native_ok;
  bool pthread_ok = run_case(true, 0);
  pthread_ok = run_case(true, 1) && pthread_ok;
  bool passed = native_ok && pthread_ok;
  printk("THREAD_TLS_QUALIFICATION_%s native=%s pthread=%s\n", passed ? "PASS" : "FAIL",
         native_ok ? "PASS" : "FAIL", pthread_ok ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
