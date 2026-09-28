// SPDX-License-Identifier: Apache-2.0

#include <stdint.h>
#include <zephyr/kernel.h>

static __thread uintptr_t thread_value;
static struct k_sem ready;
static struct k_sem release_workers;
static struct k_thread workers[2];
K_THREAD_STACK_ARRAY_DEFINE(worker_stacks, 2, 2048);
static uintptr_t *addresses[2];
static uintptr_t observed[2];

static void worker(void *argument, void *unused1, void *unused2)
{
  const uintptr_t index = (uintptr_t)argument;
  ARG_UNUSED(unused1);
  ARG_UNUSED(unused2);
  thread_value = 0x1001U + index;
  addresses[index] = &thread_value;
  k_sem_give(&ready);
  if (k_sem_take(&release_workers, K_SECONDS(2)) == 0)
    observed[index] = thread_value;
}

int main(void)
{
  /* Initialize compiler TLS before starting workers, avoiding an emulation
     allocator race that could conceal shared storage on unsupported targets. */
  thread_value = 0x2000U;
  k_sem_init(&ready, 0, 2);
  k_sem_init(&release_workers, 0, 2);
  for (uintptr_t index = 0; index < 2; index++)
    k_thread_create(&workers[index], worker_stacks[index],
                    K_THREAD_STACK_SIZEOF(worker_stacks[index]), worker,
                    (void *)index, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);

  bool ready_twice = k_sem_take(&ready, K_SECONDS(2)) == 0 &&
                     k_sem_take(&ready, K_SECONDS(2)) == 0;
  k_sem_give(&release_workers);
  k_sem_give(&release_workers);
  bool joined = k_thread_join(&workers[0], K_SECONDS(2)) == 0 &&
                k_thread_join(&workers[1], K_SECONDS(2)) == 0;
  bool isolated = ready_twice && joined && addresses[0] != addresses[1] &&
                  observed[0] == 0x1001U && observed[1] == 0x1002U &&
                  thread_value == 0x2000U;
  printk("THREAD_TLS_QUALIFICATION_%s ptr0=%p ptr1=%p value0=%lu value1=%lu main=%lu\n",
         isolated ? "PASS" : "FAIL", (void *)addresses[0], (void *)addresses[1],
         (unsigned long)observed[0], (unsigned long)observed[1],
         (unsigned long)thread_value);
  return isolated ? 0 : 1;
}
