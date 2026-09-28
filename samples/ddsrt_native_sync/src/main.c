// SPDX-License-Identifier: Apache-2.0

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <dds/ddsrt/sync.h>

K_THREAD_STACK_DEFINE(worker_stack, 2048);
static struct k_thread worker;
static struct k_sem worker_done;
static bool acquired;
static ddsrt_mutex_t test_mutex;

static void try_mutex(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  acquired = ddsrt_mutex_trylock(&test_mutex);
  if (acquired)
    ddsrt_mutex_unlock(&test_mutex);
  k_sem_give(&worker_done);
}

static void prove_mutex_owned(void)
{
  acquired = true;
  k_sem_init(&worker_done, 0, 1);
  k_tid_t tid = k_thread_create(&worker, worker_stack,
                               K_THREAD_STACK_SIZEOF(worker_stack), try_mutex,
                               NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&worker_done, K_SECONDS(2)));
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  zassert_false(acquired, "another thread acquired the returned mutex");
}

ZTEST_SUITE(ddsrt_native_sync, NULL, NULL, NULL, NULL, NULL);

ZTEST(ddsrt_native_sync, test_mutex_trylock_and_reinit)
{
  ddsrt_mutex_init(&test_mutex);
  zassert_true(ddsrt_mutex_trylock(&test_mutex));
  zassert_false(ddsrt_mutex_trylock(&test_mutex));
  prove_mutex_owned();
  ddsrt_mutex_unlock(&test_mutex);
  ddsrt_mutex_destroy(&test_mutex);
  ddsrt_mutex_init(&test_mutex);
  zassert_true(ddsrt_mutex_trylock(&test_mutex));
  ddsrt_mutex_unlock(&test_mutex);
  ddsrt_mutex_destroy(&test_mutex);
}

static ddsrt_cond_t test_cond;
static struct k_sem waiter_ready;
static bool predicate;

static void cond_waiter(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  ddsrt_mutex_lock(&test_mutex);
  k_sem_give(&waiter_ready);
  while (!predicate)
    ddsrt_cond_wait(&test_cond, &test_mutex);
  ddsrt_mutex_unlock(&test_mutex);
  k_sem_give(&worker_done);
}

ZTEST(ddsrt_native_sync, test_cond_signal_and_no_stored_token)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_init(&test_cond);
  k_sem_init(&waiter_ready, 0, 1);
  k_sem_init(&worker_done, 0, 1);
  predicate = false;
  ddsrt_cond_signal(&test_cond);
  k_tid_t tid = k_thread_create(&worker, worker_stack,
                               K_THREAD_STACK_SIZEOF(worker_stack), cond_waiter,
                               NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&waiter_ready, K_SECONDS(2)));
  zassert_not_equal(k_sem_take(&worker_done, K_MSEC(20)), 0,
                    "signal before waiting was incorrectly retained");
  ddsrt_mutex_lock(&test_mutex);
  predicate = true;
  ddsrt_cond_signal(&test_cond);
  ddsrt_mutex_unlock(&test_mutex);
  zassert_ok(k_sem_take(&worker_done, K_SECONDS(2)));
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  ddsrt_cond_destroy(&test_cond);
  ddsrt_mutex_destroy(&test_mutex);
}

ZTEST(ddsrt_native_sync, test_wall_clock_deadlines)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_wctime_t cond;
  ddsrt_cond_wctime_init(&cond);
  ddsrt_mutex_lock(&test_mutex);
  ddsrt_wctime_t now = ddsrt_time_wallclock();
  zassert_false(ddsrt_cond_wctime_waituntil(&cond, &test_mutex, now));
  zassert_false(ddsrt_cond_wctime_waituntil(
    &cond, &test_mutex, (ddsrt_wctime_t){ now.v + DDS_NSECS_IN_MSEC }));
  prove_mutex_owned();
  ddsrt_mutex_unlock(&test_mutex);
  ddsrt_cond_wctime_destroy(&cond);
  ddsrt_mutex_destroy(&test_mutex);
}

ZTEST(ddsrt_native_sync, test_monotonic_deadlines)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_mtime_t cond;
  ddsrt_cond_mtime_init(&cond);
  ddsrt_mutex_lock(&test_mutex);
  ddsrt_mtime_t now = ddsrt_time_monotonic();
  zassert_false(ddsrt_cond_mtime_waituntil(&cond, &test_mutex, now));
  zassert_false(ddsrt_cond_mtime_waituntil(
    &cond, &test_mutex, (ddsrt_mtime_t){ now.v + DDS_NSECS_IN_MSEC }));
  prove_mutex_owned();
  ddsrt_mutex_unlock(&test_mutex);
  ddsrt_cond_mtime_destroy(&cond);
  ddsrt_mutex_destroy(&test_mutex);
}

ZTEST(ddsrt_native_sync, test_elapsed_deadlines)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_etime_t cond;
  ddsrt_cond_etime_init(&cond);
  ddsrt_mutex_lock(&test_mutex);
  ddsrt_etime_t now = ddsrt_time_elapsed();
  zassert_false(ddsrt_cond_etime_waituntil(&cond, &test_mutex, now));
  zassert_false(ddsrt_cond_etime_waituntil(
    &cond, &test_mutex, (ddsrt_etime_t){ now.v + DDS_NSECS_IN_MSEC }));
  prove_mutex_owned();
  ddsrt_mutex_unlock(&test_mutex);
  ddsrt_cond_etime_destroy(&cond);
  ddsrt_mutex_destroy(&test_mutex);
}

static ddsrt_rwlock_t test_rwlock;
static struct k_sem reader_ready;
static struct k_sem reader_release;
static bool concurrent_reader;

static void read_worker(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  concurrent_reader = ddsrt_rwlock_tryread(&test_rwlock);
  k_sem_give(&reader_ready);
  k_sem_take(&reader_release, K_SECONDS(2));
  if (concurrent_reader)
    ddsrt_rwlock_unlock(&test_rwlock);
}

ZTEST(ddsrt_native_sync, test_rwlock_parallel_readers)
{
  ddsrt_rwlock_init(&test_rwlock);
  k_sem_init(&reader_ready, 0, 1);
  k_sem_init(&reader_release, 0, 1);
  concurrent_reader = false;
  ddsrt_rwlock_read(&test_rwlock);
  k_tid_t tid = k_thread_create(&worker, worker_stack,
                               K_THREAD_STACK_SIZEOF(worker_stack), read_worker,
                               NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&reader_ready, K_SECONDS(2)));
  zassert_true(concurrent_reader);
  zassert_false(ddsrt_rwlock_trywrite(&test_rwlock));
  ddsrt_rwlock_unlock(&test_rwlock);
  k_sem_give(&reader_release);
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  zassert_true(ddsrt_rwlock_trywrite(&test_rwlock));
  zassert_false(ddsrt_rwlock_tryread(&test_rwlock));
  ddsrt_rwlock_unlock(&test_rwlock);
  ddsrt_rwlock_destroy(&test_rwlock);
  ddsrt_rwlock_init(&test_rwlock);
  ddsrt_rwlock_destroy(&test_rwlock);
}

static ddsrt_once_t once_control = DDSRT_ONCE_INIT;
static int once_calls;
static int once_value;
static int observed[4];
K_THREAD_STACK_ARRAY_DEFINE(once_stacks, 4, 2048);
static struct k_thread once_threads[4];

static void once_initializer(void)
{
  once_calls++;
  k_sleep(K_MSEC(10));
  once_value = 42;
}

static void once_worker(void *a, void *b, void *c)
{
  unsigned int index = (unsigned int)(uintptr_t)a;
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  ddsrt_once(&once_control, once_initializer);
  observed[index] = once_value;
}

ZTEST(ddsrt_native_sync, test_once_publication)
{
  for (unsigned int i = 0; i < 4; i++)
    k_thread_create(&once_threads[i], once_stacks[i],
                    K_THREAD_STACK_SIZEOF(once_stacks[i]), once_worker,
                    (void *)(uintptr_t)i, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  for (unsigned int i = 0; i < 4; i++) {
    zassert_ok(k_thread_join(&once_threads[i], K_SECONDS(2)));
    zassert_equal(observed[i], 42);
  }
  zassert_equal(once_calls, 1);
  ddsrt_once(&once_control, once_initializer);
  zassert_equal(once_calls, 1);
}

K_THREAD_STACK_ARRAY_DEFINE(cond_stacks, 2, 2048);
static struct k_thread cond_threads[2];
static struct k_sem cond_ready;
static struct k_sem cond_done;

static void broadcast_waiter(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  ddsrt_mutex_lock(&test_mutex);
  k_sem_give(&cond_ready);
  while (!predicate)
    ddsrt_cond_wait(&test_cond, &test_mutex);
  ddsrt_mutex_unlock(&test_mutex);
  k_sem_give(&cond_done);
}

ZTEST(ddsrt_native_sync, test_cond_broadcast)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_init(&test_cond);
  k_sem_init(&cond_ready, 0, 2);
  k_sem_init(&cond_done, 0, 2);
  predicate = false;
  for (unsigned int i = 0; i < 2; i++)
    k_thread_create(&cond_threads[i], cond_stacks[i],
                    K_THREAD_STACK_SIZEOF(cond_stacks[i]), broadcast_waiter,
                    NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&cond_ready, K_SECONDS(2)));
  zassert_ok(k_sem_take(&cond_ready, K_SECONDS(2)));
  ddsrt_mutex_lock(&test_mutex);
  predicate = true;
  ddsrt_cond_broadcast(&test_cond);
  ddsrt_mutex_unlock(&test_mutex);
  zassert_ok(k_sem_take(&cond_done, K_SECONDS(2)));
  zassert_ok(k_sem_take(&cond_done, K_SECONDS(2)));
  for (unsigned int i = 0; i < 2; i++)
    zassert_ok(k_thread_join(&cond_threads[i], K_SECONDS(2)));
  ddsrt_cond_destroy(&test_cond);
  ddsrt_mutex_destroy(&test_mutex);
}

static ddsrt_cond_mtime_t timed_cond;
static struct k_sem timed_ready;
static struct k_sem timed_returned;
static struct k_sem timed_release;
static bool timed_result;
static int64_t timed_elapsed_ms;

static void timed_waiter(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  ddsrt_mutex_lock(&test_mutex);
  ddsrt_mtime_t now = ddsrt_time_monotonic();
  k_sem_give(&timed_ready);
  int64_t start = k_uptime_get();
  timed_result = ddsrt_cond_mtime_waituntil(
    &timed_cond, &test_mutex,
    (ddsrt_mtime_t){ now.v + 20 * DDS_NSECS_IN_MSEC });
  timed_elapsed_ms = k_uptime_get() - start;
  k_sem_give(&timed_returned);
  k_sem_take(&timed_release, K_SECONDS(2));
  ddsrt_mutex_unlock(&test_mutex);
}

ZTEST(ddsrt_native_sync, test_contended_timeout_owns_mutex)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_mtime_init(&timed_cond);
  k_sem_init(&timed_ready, 0, 1);
  k_sem_init(&timed_returned, 0, 1);
  k_sem_init(&timed_release, 0, 1);
  k_tid_t tid = k_thread_create(&worker, worker_stack,
                               K_THREAD_STACK_SIZEOF(worker_stack), timed_waiter,
                               NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&timed_ready, K_SECONDS(2)));
  ddsrt_mutex_lock(&test_mutex);
  k_sleep(K_MSEC(70));
  ddsrt_mutex_unlock(&test_mutex);
  zassert_ok(k_sem_take(&timed_returned, K_SECONDS(2)));
  zassert_false(timed_result);
  zassert_true(timed_elapsed_ms >= 70);
  zassert_false(ddsrt_mutex_trylock(&test_mutex));
  k_sem_give(&timed_release);
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  ddsrt_cond_mtime_destroy(&timed_cond);
  ddsrt_mutex_destroy(&test_mutex);
}

static void finite_signaller(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  k_sleep(K_MSEC(20));
  ddsrt_mutex_lock(&test_mutex);
  ddsrt_cond_mtime_signal(&timed_cond);
  ddsrt_mutex_unlock(&test_mutex);
}

ZTEST(ddsrt_native_sync, test_large_finite_and_infinite_deadlines)
{
  ddsrt_mutex_init(&test_mutex);
  ddsrt_cond_mtime_init(&timed_cond);
  ddsrt_mutex_lock(&test_mutex);
  k_tid_t tid = k_thread_create(&worker, worker_stack,
                               K_THREAD_STACK_SIZEOF(worker_stack), finite_signaller,
                               NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_true(ddsrt_cond_mtime_waituntil(
    &timed_cond, &test_mutex, (ddsrt_mtime_t){ INT64_MAX - 2 }));
  ddsrt_mutex_unlock(&test_mutex);
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  ddsrt_mutex_lock(&test_mutex);
  tid = k_thread_create(&worker, worker_stack,
                        K_THREAD_STACK_SIZEOF(worker_stack), finite_signaller,
                        NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_true(ddsrt_cond_mtime_waituntil(&timed_cond, &test_mutex, DDSRT_MTIME_NEVER));
  ddsrt_mutex_unlock(&test_mutex);
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  ddsrt_cond_mtime_destroy(&timed_cond);
  ddsrt_mutex_destroy(&test_mutex);
}

static struct k_sem writer_ready;
static struct k_sem writer_done;
static unsigned int writer_value;

static void write_worker(void *a, void *b, void *c)
{
  ARG_UNUSED(a);
  ARG_UNUSED(b);
  ARG_UNUSED(c);
  k_sem_give(&writer_ready);
  ddsrt_rwlock_write(&test_rwlock);
  writer_value = 1;
  ddsrt_rwlock_unlock(&test_rwlock);
  k_sem_give(&writer_done);
}

ZTEST(ddsrt_native_sync, test_rwlock_writer_progress)
{
  ddsrt_rwlock_init(&test_rwlock);
  k_sem_init(&writer_ready, 0, 1);
  k_sem_init(&writer_done, 0, 1);
  writer_value = 0;
  ddsrt_rwlock_read(&test_rwlock);
  k_tid_t tid = k_thread_create(&worker, worker_stack,
                               K_THREAD_STACK_SIZEOF(worker_stack), write_worker,
                               NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&writer_ready, K_SECONDS(2)));
  zassert_not_equal(k_sem_take(&writer_done, K_MSEC(20)), 0);
  ddsrt_rwlock_unlock(&test_rwlock);
  zassert_ok(k_sem_take(&writer_done, K_SECONDS(2)));
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  zassert_equal(writer_value, 1);
  ddsrt_rwlock_destroy(&test_rwlock);
}
