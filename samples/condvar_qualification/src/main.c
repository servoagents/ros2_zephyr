// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

enum wait_case { WAIT_SIGNAL, WAIT_TIMEOUT, WAIT_ZERO, WAIT_CONTENDED_TIMEOUT };

struct case_state {
  bool posix;
  enum wait_case mode;
  struct k_mutex mutex;
  struct k_condvar cond;
  pthread_mutex_t pmutex;
  pthread_cond_t pcond;
  struct k_sem ready;
  struct k_sem returned;
  struct k_sem release;
  int result;
  int64_t elapsed_ms;
  bool contender_acquired;
};

K_THREAD_STACK_DEFINE(waiter_stack, 2048);
static struct k_thread waiter_thread;
static struct case_state state;

static void lock_case(struct case_state *s)
{
  if (s->posix) {
    zassert_ok(pthread_mutex_lock(&s->pmutex));
  } else {
    zassert_ok(k_mutex_lock(&s->mutex, K_FOREVER));
  }
}

static void unlock_case(struct case_state *s)
{
  if (s->posix) {
    zassert_ok(pthread_mutex_unlock(&s->pmutex));
  } else {
    zassert_ok(k_mutex_unlock(&s->mutex));
  }
}

static void wait_thread(void *arg1, void *arg2, void *arg3)
{
  struct case_state *s = arg1;
  ARG_UNUSED(arg2);
  ARG_UNUSED(arg3);
  lock_case(s);
  k_sem_give(&s->ready);
  const int64_t start = k_uptime_get();
  if (s->posix) {
    if (s->mode == WAIT_SIGNAL) {
      s->result = pthread_cond_wait(&s->pcond, &s->pmutex);
    } else {
      struct timespec deadline;
      zassert_ok(clock_gettime(CLOCK_REALTIME, &deadline));
      if (s->mode != WAIT_ZERO) {
        deadline.tv_nsec += 30 * 1000 * 1000;
        if (deadline.tv_nsec >= 1000 * 1000 * 1000) {
          deadline.tv_nsec -= 1000 * 1000 * 1000;
          deadline.tv_sec++;
        }
      }
      s->result = pthread_cond_timedwait(&s->pcond, &s->pmutex, &deadline);
    }
  } else {
    const k_timeout_t timeout = s->mode == WAIT_SIGNAL ? K_FOREVER :
                                s->mode == WAIT_ZERO ? K_NO_WAIT : K_MSEC(30);
    s->result = k_condvar_wait(&s->cond, &s->mutex, timeout);
  }
  s->elapsed_ms = k_uptime_get() - start;
  k_sem_give(&s->returned);
  zassert_ok(k_sem_take(&s->release, K_SECONDS(2)));
  if (!s->contender_acquired) {
    unlock_case(s);
  }
}

static void run_case(bool posix, enum wait_case mode)
{
  struct case_state *s = &state;
  *s = (struct case_state){.posix = posix, .mode = mode};
  zassert_ok(k_mutex_init(&s->mutex));
  zassert_ok(k_condvar_init(&s->cond));
  zassert_ok(pthread_mutex_init(&s->pmutex, NULL));
  zassert_ok(pthread_cond_init(&s->pcond, NULL));
  k_sem_init(&s->ready, 0, 1);
  k_sem_init(&s->returned, 0, 1);
  k_sem_init(&s->release, 0, 1);

  k_tid_t tid = k_thread_create(&waiter_thread, waiter_stack,
                               K_THREAD_STACK_SIZEOF(waiter_stack), wait_thread,
                               s, NULL, NULL, K_PRIO_PREEMPT(5), 0, K_NO_WAIT);
  zassert_ok(k_sem_take(&s->ready, K_SECONDS(2)));
  if (mode == WAIT_SIGNAL || mode == WAIT_CONTENDED_TIMEOUT) {
    lock_case(s);
    if (mode == WAIT_SIGNAL) {
      if (posix) {
        zassert_ok(pthread_cond_signal(&s->pcond));
      } else {
        zassert_ok(k_condvar_signal(&s->cond));
      }
    } else {
      k_sleep(K_MSEC(70));
    }
    unlock_case(s);
  }
  zassert_ok(k_sem_take(&s->returned, K_SECONDS(2)));
  if (posix) {
    const int ret = pthread_mutex_trylock(&s->pmutex);
    s->contender_acquired = ret == 0;
    zassert_true(ret == 0 || ret == EBUSY);
  } else {
    const int ret = k_mutex_lock(&s->mutex, K_NO_WAIT);
    s->contender_acquired = ret == 0;
    zassert_true(ret == 0 || ret == -EBUSY);
  }
  if (s->contender_acquired) {
    unlock_case(s);
  }
  k_sem_give(&s->release);
  zassert_ok(k_thread_join(tid, K_SECONDS(2)));
  zassert_ok(pthread_cond_destroy(&s->pcond));
  zassert_ok(pthread_mutex_destroy(&s->pmutex));
  zassert_equal(s->result, mode == WAIT_SIGNAL ? 0 : posix ? ETIMEDOUT : -EAGAIN);
  if (mode == WAIT_CONTENDED_TIMEOUT) {
    zassert_true(s->elapsed_ms >= 70,
                 "timed-out waiter returned before the contending thread released the mutex");
  }
  zassert_false(s->contender_acquired,
                "waiter returned without owning the mutex; another thread acquired it");
}

ZTEST_SUITE(condvar_qualification, NULL, NULL, NULL, NULL, NULL);

ZTEST(condvar_qualification, test_direct_signal) { run_case(false, WAIT_SIGNAL); }
ZTEST(condvar_qualification, test_direct_timeout) { run_case(false, WAIT_TIMEOUT); }
ZTEST(condvar_qualification, test_direct_zero) { run_case(false, WAIT_ZERO); }
ZTEST(condvar_qualification, test_direct_contended_timeout) { run_case(false, WAIT_CONTENDED_TIMEOUT); }
ZTEST(condvar_qualification, test_posix_signal) { run_case(true, WAIT_SIGNAL); }
ZTEST(condvar_qualification, test_posix_timeout) { run_case(true, WAIT_TIMEOUT); }
ZTEST(condvar_qualification, test_posix_zero) { run_case(true, WAIT_ZERO); }
ZTEST(condvar_qualification, test_posix_contended_timeout) { run_case(true, WAIT_CONTENDED_TIMEOUT); }
