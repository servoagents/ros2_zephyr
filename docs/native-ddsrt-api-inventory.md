# Pinned DDSRT API and dependency inventory (Milestone A)

This inventory is for Cyclone DDS `2f0d07d241f62f7121749b46721049e4dea5c58b`,
Zephyr `684c9e8f32e4373a21098559f748f06915f950c9` (v4.4.0), and the
Lyrical `rmw_cyclonedds_c` pin `039e85860dc6032d51bd25dc552e5bdbbb0e686a`.
Paths in the table are relative to the Cyclone repository. No native DDSRT
implementation is present yet. "Proposed" entries are design candidates,
not assertions that their semantics have been proved.

`src/ddsrt/CMakeLists.txt` selects the Zephyr branch with `WITH_ZEPHYR=ON`,
but that branch selects `sync/posix.h`, `threads/posix.h`, `types/posix.h`,
`sync/posix/sync.c`, `threads/posix/threads.c`, and `time/posix/time.c`.
It links `Threads::Threads`. The generated `dds/config.h` has
`DDSRT_HAVE_CONDATTR_SETCLOCK=1`; the selected types contain actual pthread
objects. The word `posix` in `heap/posix`, `process/posix`, `random/posix`, or
`sockets/posix` does **not** itself prove a pthread dependency. Zephyr uses
`environ/zephyr` and `ifaddrs/zephyr` despite the selected POSIX threading.
The fixed native `libddsc.a` confirms the selection at object level:
`sync.c.obj` defines `ddsrt_cond_etime_waituntil` and `ddsrt_rwlock_read` but
references `pthread_cond_timedwait` and `pthread_rwlock_rdlock`;
`threads.c.obj` defines `ddsrt_thread_create` and references `pthread_create`;
`time.c.obj` references `clock_gettime`. Core objects reference those DDSRT
symbols. This is target-library evidence, not a host-simulator pthread count.
The fixed native `libddsc.a` confirms the selection at object level:
`sync.c.obj` defines `ddsrt_cond_etime_waituntil` and `ddsrt_rwlock_read` but
references `pthread_cond_timedwait` and `pthread_rwlock_rdlock`;
`threads.c.obj` defines `ddsrt_thread_create` and references `pthread_create`;
`time.c.obj` references `clock_gettime`. Core objects reference those DDSRT
symbols. This is target-library evidence, not a host-simulator pthread count.

| DDSRT API | Source/header | Selected implementation | OS calls/types | Callers | Lifecycle/error contract | Test | Proposed native mapping |
|---|---|---|---|---|---|---|---|
| Mutex init/destroy/lock/trylock/unlock | `sync.h`, `sync/posix.h`, `sync/posix/sync.c` | POSIX sync | `pthread_mutex_t`, `pthread_mutex_*` | DDSC entities/RHC; DDSI address sets/endpoints/GC; DDSRT log/cdtors | Void calls abort on unexpected error; trylock false only when busy | Associated-mutex ownership in condvar qualification; full primitive tests deferred | `k_mutex`, with tested recursion, contention and object lifetime |
| Generic cond init/destroy/wait/signal/broadcast | Same sync files | POSIX sync | `pthread_cond_t`, `pthread_cond_*` | DDSC entity observers/status; DDSI queues | Wait returns with associated mutex; unexpected error aborts; no stored signal token | Underlying pthread signal/ownership tested | `k_condvar` after kernel fix; test signal/broadcast/no-token |
| Wall-clock cond init/destroy/wait/waituntil/signal/broadcast | Same sync files | POSIX sync | Cond attr `CLOCK_REALTIME`, absolute `timespec` | Public DDSRT API; no selected core waituntil caller found | `DDS_NEVER` untimed; expired deadline false; mutex held | Underlying pthread timeout tested, DDSRT wrapper not directly tested | `k_condvar` plus checked wall epoch deadline conversion |
| Monotonic cond init/destroy/wait/waituntil/signal/broadcast | Same sync files | POSIX sync | Cond attr `CLOCK_MONOTONIC`, absolute `timespec` | DDSI throttle/transmit/xevent/thread monitor | Same bool/ownership rules, monotonic domain | Underlying pthread timeout tested, wrapper deferred | `k_condvar` plus checked monotonic conversion |
| Elapsed cond init/destroy/wait/waituntil/signal/broadcast | Same sync files | POSIX sync | Cond attr `CLOCK_MONOTONIC`, absolute `timespec` | DDSC waitset/topic; DDSI GC/typelib | Same bool/ownership rules, elapsed domain | Underlying pthread timeout tested, wrapper deferred | `k_condvar` plus checked elapsed conversion |
| Rwlock init/destroy/read/write/tryread/trywrite/unlock | `sync.h`, `sync/posix.h`, `sync/posix/sync.c` | POSIX sync | `pthread_rwlock_t`, `pthread_rwlock_*` | DDSRT log; public API | Parallel readers, exclusive writer, busy try returns false; other error asserts | Deferred | Native reader/writer scheme preserving parallel reads and progress |
| Once; `DDSRT_ONCE_INIT` | Same sync files | POSIX sync | `pthread_once_t`, `PTHREAD_ONCE_INIT`, `pthread_once` | DDSRT log; DDSI plist/fakenet; cleanup key | Static initializer; exactly-once completion publication | Deferred | Native blocking once with static initializer |
| Thread attributes/create | `threads.h`, `threads/posix.h`, `src/threads.c`, `threads/posix/threads.c` | POSIX threads with Zephyr stack array | `pthread_attr_*`, `pthread_create`, `K_THREAD_STACK_ARRAY_DEFINE` | DDSI worker creation | Class/priority/affinity/stack contract; unsupported affinity fails; child frees allocated name/context; stack index never reused | Deferred; no native threads in A | Later bounded native records/stacks and explicit priority translation |
| Join/self/equal/ID/name | `threads.h`, `threads/posix.h`, `threads/posix/threads.c` | POSIX threads | `pthread_join/self/equal`, name calls, `pthread_t` | DDSI state, receive owner, GC shutdown | Join returns full `uint32_t` result; Zephyr pthread ID zero valid; name truncates/falls back | Deferred | Later `k_thread_join`, retained result and pointer-safe identity |
| Cleanup push/pop/init/fini | `threads.h`, `threads/posix/threads.c` | POSIX TLS-key cleanup | `pthread_key_*`, `pthread_once`; heap node per push | DDSI lazy external thread-state registration; cdtors | LIFO destructor; pop execute/discard; native Zephyr non-pthread returns UNSUPPORTED | Deferred | Later worker-local cleanup plus explicit external-thread contract |
| `ddsrt_thread_local` | `threads.h` | Compiler TLS | `__thread`, not pthread key | DDSI state/freelist; DDSRT log buffer | Isolated storage, no automatic destructor by itself | Deferred | Preserve only with tested Zephyr TLS isolation; cleanup separately |
| Wall/monotonic/elapsed/highres clocks | `time.h`, `time/posix/time.c` | POSIX time | `clock_gettime(CLOCK_REALTIME/MONOTONIC)` | DDSI deadlines/events; DDSC waits/log | Wall has Unix epoch; elapsed/monotonic share implementation here, not API meaning | Existing ROS runtime; clock-specific tests deferred | Retain wall service; assess native monotonic/highres later |
| `dds_sleepfor` | `src/time.c` | POSIX sleep | `nanosleep` with EINTR retry | DDSI/public DDS | Negative no-op; finite sleep retries remaining time | Deferred | Later `k_sleep` after rounding/range checks |
| Init/fini, constructors/finalizers | `src/cdtors.c`, `cdtors.h` | Atomic DDSRT init count plus platform hooks | Mutex/cond; platform constructor hooks | DDS lifecycle | Final count tears down globals; POSIX key destructor handles pthread exit | Existing loopback cleanup; dedicated lifecycle test deferred | Preserve ordering and external-thread cleanup contract |

Selected core files have no general direct pthread calls outside DDSRT. The
`pthread_kill` stack-trace diagnostic in `src/core/ddsi/src/ddsi_sysdeps.c`
is Linux-guarded, not Zephyr-selected. The RMW pin's principal
`rmw_cyclonedds_c.c` uses DDS public APIs and has no direct pthread or DDSRT
primitive calls. This does not make the entire ROS image pthread-free:
`rcutils/src/base64.c` uses `pthread_once`, and
`rcl_yaml_param_parser/src/parse.c` has a pthread-once fallback. Inclusion in
the final linked image requires object-level proof, not just source grep.

The actual static-control `.config` capacities are native: 10 POSIX threads,
300 mutexes, 150 condvars, 5 rwlocks, 32 keys, 5 barriers; ESP checkpoint:
8 threads, 256 mutexes, 96 condvars, 5 rwlocks, 8 keys, 5 barriers. Native
ELF pool symbols occupy 3,440 + 9,600 + 3,600 + 640 + 768 + 280 = 18,328 B.
The ESP checkpoint symbols occupy 1,792 + 5,120 + 1,152 + 380 + 96 + 180 =
8,720 B. These are linked storage, not guaranteed savings: other users may
still need pools, and new native objects would consume RAM. DDSRT worker
stacks are already native static reservations: 8 × 65,536 = 524,288 B on
native static control and 5 × 8,192 = 40,960 B on ESP. The control stack is
another 2,048 B. The accepted control thread is Zephyr priority 3; main is
4; existing worker scheduling must be measured rather than inferred from
POSIX numeric priorities. On native loopback, reported DDS worker priority
was 14. The current POSIX stack index has no create/join-cycle reuse; this
is a lifecycle concern for later work, not license to implement threads in A.
