# Zephyr-native DDSRT synchronization

Native Zephyr synchronization is selectable and passes the tested static-control workload on `native_sim` and
ESP32-S3. Cyclone DDS threads, clocks, sockets and several other DDSRT services
remain POSIX-backed. This is not a native-thread or POSIX-free port.

## Revisions and selection

- Cyclone fork `fj-blanco/cyclonedds`, branch `zephyr-native-ddsrt`, exact
  published commit `31e9ea8a255092eab29cea6d3ee6505410c903a5`.
  `193e9c38` adds the already accepted Zephyr 4.4 compatibility changes;
  `31e9ea8a` adds native sync. Both descend directly from the prior integration
  pin `2f0d07d241f62f7121749b46721049e4dea5c58b`. No unrelated rebase.
- `dependencies/platform.repos` pins that published SHA; the staged checkout
  is clean at it. The compatibility-patch setup script detects the committed
  patch and remains idempotent. No Cyclone source was copied into this repo.
- `ROS2_ZEPHYR_CYCLONEDDS_NATIVE_SYNC=ON` selects
  `WITH_ZEPHYR_NATIVE_SYNC=ON` for the Cyclone ExternalProject. Both default
  OFF. The Cyclone option rejects non-Zephyr builds. Its generated `config.h`
  chooses the matching native type definitions and implementation; pristine
  POSIX and native build directories were used for the controlled comparison.
- Zephyr v4.4.0 retains the separately qualified
  `patches/zephyr-condvar-timeout-relock.patch`. RMW remains pinned at
  `039e85860dc6032d51bd25dc552e5bdbbb0e686a` with its accepted wait-set
  patch. ROS 2 Lyrical package pins are unchanged.

The selected `ddsrt_sync` is now carried into pre-link and post-link resource
reports (`posix` or `zephyr-native`). Build caches corroborate the selection:
the native Cyclone cache has `WITH_ZEPHYR_NATIVE_SYNC:BOOL=ON`, and its
generated header defines `DDSRT_WITH_ZEPHYR_NATIVE_SYNC`; the POSIX build has
the option OFF and the define absent. `nm -u` on the selected sync object
references Zephyr `k_mutex`/`k_condvar` operations and no pthread sync calls;
the POSIX object's undefined symbols include `pthread_cond_*`. The native
image's still-selected `threads/posix/threads.c.obj` references
`pthread_create` and `pthread_join`. `Threads::Threads` is intentionally kept.

## Contract and tests

The native implementation uses `k_mutex`, `k_condvar`, a guarded native
reader/writer lock with overlapping readers and waiting-writer preference,
and a statically initialized once guard/condition. Mutex/condition destruction
rejects active ownership/waiters; reinitialization of idle objects is tested.
`ddsrt_mutex_trylock` preserves the POSIX self-try failure despite Zephyr's
recursive mutex. Reader/writer try operations use `K_NO_WAIT`. Once waiters
observe the initializer's published data; none spin or allocate.

Wall-clock, monotonic and elapsed condition deadlines use their respective
existing DDSRT POSIX clock services. `DDS_NEVER` alone means infinite wait;
other finite deadlines, including near-`INT64_MAX`, are chunked when needed.
Positive intervals round up to a kernel tick, already-expired deadlines
return false, and retries retain the original absolute deadline. A timed
wait returns holding the associated mutex even after timeout, relying on the
separately qualified kernel condition-variable repair. A wall-clock jump can
change the remaining interval when the next chunk is calculated; no stronger
clock-adjustment guarantee is claimed.

`west twister -T samples/ddsrt_native_sync -p native_sim/native/64 --outdir
build/phase4-b/native-sync-twister-final --inline-logs` passed **11/11**
cases, including contention, try/reinit, no retained signal, all three clock
domains, past/sub-tick/large-finite/infinite waits, mutex ownership on
timeout, broadcast, concurrent readers, writer progress and once publication.
The test uses explicit handshakes and bounded joins. Host Cyclone POSIX
`ddsrt_sync_*` tests passed **8/8**; non-Zephyr native selection was rejected.
The project check script passed **21/21** unit tests. A nested read acquired
after a writer has queued is not supported by this writer-preferring design;
the audited DDSRT runtime read-lock caller is `src/ddsrt/src/log.c`, which
does not nest its read lock. This policy needs explicit maintainer review
before broadening the supported workloads.

## Matched workload and resource evidence

Uninstrumented `static_control` used the same local fork and deployment for
both arms, with native sync OFF versus ON. Five native-simulator windows per
arm lasted about 9 s (eight valid one-second metric records each). Max
adjacent middleware allocation-attempt deltas were:

| Run | POSIX | Native sync |
|---:|---:|---:|
| 1 | 21 | 29 |
| 2 | 29 | 21 |
| 3 | 29 | 29 |
| 4 | 21 | 21 |
| 5 | 29 | 29 |

All are below the accepted quiet-graph limit of 32; ROS steady allocations
and skipped control periods were zero. A further 14-record final-code native
simulator run also passed (max adjacent delta 29; 2,008 B control-stack
unused). Native-sync loopback passed with zero steady ROS allocations and
clean shutdown. The final uninstrumented ESP32-S3 build linked at
**1,013,044 B flash / 262,216 B static RAM**, leaving **21,400 B** under the
300,000 B RAM budget after the 16,384 B application reserve. Its final
12-record hardware window passed: max adjacent middleware delta 25, zero
steady ROS allocations, zero skipped periods and 1,664 B control-stack
unused. Before the last nonblocking-try correction, three other 12-record
hardware windows and a typed stock Lyrical Cyclone peer DISABLED → ACTIVE →
STALE exchange also passed; those earlier runs are retained as integration
evidence, not mislabelled as runs of the final object code.

A separate clean `pinned-native-sync-loopback` build used the staged manifest
checkout at `31e9ea8a`, with no local-source override. Its generated resource
report says `ddsrt_sync=zephyr-native`, and the runner again printed
`ROS2_ZEPHYR_LOOPBACK_PASS`. This confirms the published pin can build through
the normal dependency path, not only from the editable fork tree.

The controlled native-simulator static images linked at 719,984 B flash /
1,224,121 B RAM (POSIX) and 719,423 B flash / 1,223,885 B RAM (native
sync): native sync was 561 B smaller in flash and 236 B smaller in RAM for
that pairing. These small differences do **not** imply removal of the Zephyr
POSIX thread/cond/mutex pools; those configured capacities remain. An earlier image has different provenance and is excluded from this
size comparison.

An optional `ROS2_ZEPHYR_STATIC_CONTROL_TIMING_PROBE=ON` records timer
release and worker-completion lateness in 100 µs histogram buckets plus
observed maxima using the ESP32 32-bit cycle counter. The first 64-bit
counter attempt produced zero samples on ESP32 and was discarded. Native
simulator virtual cycle time does not advance during execution, so its
timing histograms are not interpreted as physical latency. Three final-code
ESP32 native-sync windows were paired with three POSIX windows; each has
about 1,200 samples (12 one-second records). All release and completion
p50/p95/p99 values were in the first **≤100 µs** bucket in both arms.
Observed release maxima were 7/7/7 µs (POSIX) versus 8/6/6 µs (native),
completion lateness maxima 0 µs in both. The declared +1,000 µs p99/max
regression gate passed all three pairs. This is a bounded workload
observation, not a hard real-time guarantee. The instrumented POSIX image
used 1,012,964 B flash / 262,800 B RAM; native used 1,013,232 B /
263,056 B. Do not compare their image sizes with the uninstrumented pair.

Build reports, test output and UART traces were kept in local `build/`
directories; they are not part of the source release. The board ran the
instrumented native-sync image for the timing comparison.

## Reproduce and decision

Source the provisioned `build/zephyr-env-lyrical.sh` first. For a pristine
native comparison, set `ROS2_ZEPHYR_CYCLONEDDS_SOURCE_OVERRIDE` to the local
fork path, choose separate build directories, and invoke the existing
`samples/static_control/build_native.sh` with
`ROS2_ZEPHYR_CYCLONEDDS_NATIVE_SYNC=OFF` and then `ON`. The ESP build uses
`samples/static_control/build_esp32.sh` with the same selector and separate
build directories; export `WIFI_SSID`, `WIFI_PSK`, and `ROS_PEER_IP` from your
own environment rather than writing them to a tracked file. Set the timing
probe flag only for matched instrumentation runs. Compare with
`python tools/check_static_control_timing.py --posix <three logs> --native
<three logs> --minimum-records 12 --maximum-regression-us 1000`.

This is a partial native port. The reader/writer lock fairness and nested-read
policy need maintainer review and broader stress coverage. Native DDSRT
threading still requires explicit ownership, join-result and stack-lifecycle
design, plus target TLS qualification.
