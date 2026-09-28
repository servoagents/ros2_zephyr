# Phase 4 Milestone C gate: ESP32-S3 compiler TLS

Status: **stop native DDSRT thread implementation on this target**. The
standalone qualification test demonstrates that `__thread` storage aliases
across main, native Zephyr workers, and Zephyr pthread workers on the pinned
ESP32-S3 build. No Cyclone source was modified during this check. The accepted
ROS/Cyclone integration runs remain real observations, but they did not test
TLS isolation and cannot establish it.

## Exact scope and procedure

- Target: `esp32s3_devkitc/esp32s3/procpu`, native USB serial, Zephyr v4.4.0
  `684c9e8f32e4373a21098559f748f06915f950c9`, Zephyr SDK 1.0.1 with
  Xtensa GCC 14.3.0. The separately accepted condition-variable repair is
  present in the workspace; this test does not use condition variables.
- Cyclone fork branch `zephyr-native-ddsrt` remains at
  `31e9ea8a255092eab29cea6d3ee6505410c903a5`. The existing RMW pin and
  all accepted Milestone B changes are untouched.
- `samples/thread_tls_qualification` uses one `static __thread volatile
  uint32_t` and the same worker body for two native `k_thread` workers, then
  two Zephyr `pthread_create` workers. The main thread initializes its own
  TLS first; each worker records its address and distinct initial value.
  A ready barrier and 2,000 semaphore handoffs with `k_yield()` exercise
  context switches. Each worker checks its value twice per handoff; the
  test also checks final values and main/worker address separation. Zephyr
  pthreads use the same static stack reservations after the native workers
  have fully joined, because this configuration does not enable dynamic
  thread stacks.
- `native_sim/native/64` passed both cases: distinct main/worker addresses,
  expected first and last values, zero mismatches, zero timeouts.
- The ESP32-S3 image was rebuilt with the native-USB console overlay, flashed
  and verified, then captured after four resets. The earlier UART0-console
  capture was discarded as inconclusive; it produced no Zephyr test marker.

| Hardware run | API | Main / A / B TLS address | A / B last value | Mismatches A / B | Timeouts |
|---:|---|---|---|---:|---:|
| 1 | native | `0x3fc9ae3c` / same / same | `22222222` / `22222222` | 4000 / 0 | 0 / 0 |
| 1 | pthread | `0x3fc9ae3c` / same / same | `22222222` / `22222222` | 4000 / 0 | 0 / 0 |
| 2 | native and pthread | same address and values as run 1 | same | 4000 / 0 each | 0 / 0 |
| 3 | native and pthread | same address and values as run 1 | same | 4000 / 0 each | 0 / 0 |
| 4 (final warning-clean build) | native and pthread | same address and values as run 1 | same | 4000 / 0 each | 0 / 0 |

All four final markers were `THREAD_TLS_QUALIFICATION_FAIL native=FAIL
pthread=FAIL`. The 4,000 mismatches are two checks per handoff for worker A;
they are not a creation, join or scheduling failure. Full UART logs are in
ignored `build/phase4-c/tls-esp32s3-usb-run-{1,2,3}.log` and
`build/phase4-c/tls-esp32s3-usb-run-final.log`; build logs,
ELF, map and `.config` remain alongside them. No Wi-Fi credentials were
used. The board currently has this diagnostic firmware flashed.

## Cause and impact boundary

Neither `CONFIG_THREAD_LOCAL_STORAGE` nor
`CONFIG_ARCH_HAS_THREAD_LOCAL_STORAGE` is enabled in the ESP32-S3 probe or
the accepted POSIX-Cyclone image. Zephyr's TLS documentation says compiler
TLS requires the former and architecture/toolchain support for it. The linked
Xtensa `libgcc.a(emutls.o)` exports `__emutls_get_address`; disassembly of
the probe shows it reads and writes the address field in one global control
object, with no current-thread lookup. The hardware test independently
confirms the resulting aliasing for this build.

This is **not limited to proposed native DDSRT threads**. The existing
`build/phase4-b/posix-timing-esp32s3` image has
`WITH_ZEPHYR_NATIVE_SYNC:BOOL=OFF`, `CONFIG_POSIX_THREADS=y`, the same
`__emutls_get_address`, and linked control objects
`__emutls_v$tsd_thread_state`, `__emutls_v$log_buffer` and
`__emutls_v$freelist_inner_idx`. Its map attributes `tsd_thread_state` to
Cyclone's `ddsi_thread.c.obj`. That source assigns the `ddsrt_thread_local`
pointer in worker and application threads. **Inference from the identical
toolchain accessor plus the direct pthread probe:** the accepted Cyclone
POSIX build is exposed to the same lack of compiler-TLS isolation. The
previous ROS pub/sub and static-control successes do not prove that DDSI
bookkeeping remained thread-isolated; they also do not by themselves show
a user-visible failure in those workloads. A focused DDSI-level safety audit
belongs in the next investigation.

Zephyr's [TLS documentation](https://docs.zephyrproject.org/latest/kernel/services/other/thread_local_storage.html)
and [architecture porting guide](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/hardware/porting/arch.rst)
describe the intended fix: per-thread TLS stack setup, restoration of the
TLS base on context switches, architecture capability selection and the
standard kernel TLS tests. This is an architecture/toolchain task, not a
small DDSRT thread-lifecycle patch.

## Decision

Milestone C is paused under the Phase 4 stop rule: preserving required
per-thread behavior on ESP32-S3 would require a broader Zephyr TLS port or
invasive DDS-core/toolchain emulation. Do **not** implement a Cyclone TLS
workaround or claim that keeping pthread workers solves this on the tested
configuration. The smallest justified next task is to qualify or add proper
Xtensa/ESP32-S3 TLS support in Zephyr as a separate, explicitly scoped
milestone, then rerun this probe and the accepted Cyclone workloads before
resuming native DDSRT thread lifecycle work. Milestone B native
synchronization remains an independent, measured intermediate result, but
the earlier whole-workload acceptance should not be read as a TLS proof.
