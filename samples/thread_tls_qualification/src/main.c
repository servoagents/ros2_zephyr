// SPDX-License-Identifier: Apache-2.0

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#define HANDOFFS 2000U
#define MAIN_VALUE 0x33333333U

static __thread volatile uint32_t thread_value;
static struct k_sem ready;
static struct k_sem done;
static struct k_sem turn[2];
static struct k_thread native_workers[2];
K_THREAD_STACK_ARRAY_DEFINE(native_stacks, 2, 4096);

struct observation {
  uintptr_t address;
  uint32_t first;
  uint32_t last;
  uint32_t mismatches;
  uint32_t timeouts;
};

static struct observation observed[2];

static uint32_t expected_value(uintptr_t index)
{
  return index == 0 ? 0x11111111U : 0x22222222U;
}

/* Both thread APIs execute this exact body and touch the same TLS variable. */
static void *worker(void *argument)
{
  uintptr_t index = (uintptr_t)argument;
  const uint32_t expected = expected_value(index);
  thread_value = expected;
  observed[index].address = (uintptr_t)&thread_value;
  observed[index].first = thread_value;
  k_sem_give(&ready);

  for (unsigned int step = 0; step < HANDOFFS; step++) {
    if (k_sem_take(&turn[index], K_SECONDS(2)) != 0) {
      observed[index].timeouts++;
      break;
    }
    if (thread_value != expected)
      observed[index].mismatches++;
    k_yield();
    if (thread_value != expected)
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

static bool run_case(bool pthread_mode)
{
  pthread_t posix_workers[2];
  pthread_attr_t posix_attrs[2];
  const char *mode = pthread_mode ? "pthread" : "native";
  unsigned int created = 0;
  bool created_all = true;

  /* Allocate emulated TLS before creating workers, so an allocator race
     cannot accidentally produce distinct worker addresses. */
  thread_value = MAIN_VALUE;
  uintptr_t main_address = (uintptr_t)&thread_value;
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
        result = pthread_create(&posix_workers[index], &posix_attrs[index],
                                worker, (void *)index);
      if (result != 0) {
        printk("THREAD_TLS_CREATE_FAIL mode=%s code=%d\n", mode, result);
        if (attr_initialized)
          (void)pthread_attr_destroy(&posix_attrs[index]);
        created_all = false;
        break;
      }
    } else {
      k_thread_create(&native_workers[index], native_stacks[index],
                      K_THREAD_STACK_SIZEOF(native_stacks[index]), native_worker,
                      (void *)index, NULL, NULL, K_PRIO_PREEMPT(5), 0,
                      K_NO_WAIT);
    }
    created++;
  }

  bool ready_twice = created_all && k_sem_take(&ready, K_SECONDS(5)) == 0 &&
                     k_sem_take(&ready, K_SECONDS(5)) == 0;
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

  bool isolated = created_all && ready_twice && finished && joined &&
                  main_address != observed[0].address &&
                  main_address != observed[1].address &&
                  observed[0].address != observed[1].address &&
                  observed[0].first == expected_value(0) &&
                  observed[1].first == expected_value(1) &&
                  observed[0].last == expected_value(0) &&
                  observed[1].last == expected_value(1) &&
                  observed[0].mismatches == 0 && observed[1].mismatches == 0 &&
                  observed[0].timeouts == 0 && observed[1].timeouts == 0 &&
                  thread_value == MAIN_VALUE;
  printk("THREAD_TLS_CASE mode=%s result=%s main=%p a=%p b=%p "
         "first=%x/%x last=%x/%x mismatches=%u/%u timeouts=%u/%u handoffs=%u\n",
         mode, isolated ? "PASS" : "FAIL", (void *)main_address,
         (void *)observed[0].address, (void *)observed[1].address,
         observed[0].first, observed[1].first,
         observed[0].last, observed[1].last,
         observed[0].mismatches, observed[1].mismatches,
         observed[0].timeouts, observed[1].timeouts, HANDOFFS);
  return isolated;
}

int main(void)
{
  bool native_ok = run_case(false);
  bool pthread_ok = run_case(true);
  bool passed = native_ok && pthread_ok;
  printk("THREAD_TLS_QUALIFICATION_%s native=%s pthread=%s\n",
         passed ? "PASS" : "FAIL", native_ok ? "PASS" : "FAIL",
         pthread_ok ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
