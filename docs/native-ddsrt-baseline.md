# Phase 4 Milestone A baseline and comparison policy

Status: native qualification complete; ESP32-S3 kernel test and loopback
execute successfully, but the full static-control image has not reached
Zephyr startup on the current UART-only power setup. No native DDSRT
synchronization or thread implementation has been introduced. Generated
artifacts and runtime logs are preserved in the ignored `build/phase4-a/`
tree, not copied into tracked source.

## Exact inputs

- Project: `8873dfca56999a0641d8aef902848a2e7a7323bb` (`compiler`),
  plus the Milestone A changes listed in the audit. A pre-existing untracked
  `ram.json` was left untouched.
- Cyclone: local fork `cyclonedds`, branch `zephyr-native-ddsrt`, exact
  `2f0d07d241f62f7121749b46721049e4dea5c58b`. The staged checkout is
  at that SHA with the accepted `patches/cyclonedds-zephyr-4.4.patch` applied.
  `dependencies/platform.repos` now uses `fj-blanco/cyclonedds` at the SHA;
  the staged checkout's origin was also aligned with the fork. The branch is
  at the exact accepted SHA and now tracks `origin/zephyr-native-ddsrt` at
  that SHA; no push or upstream rebase was performed as part of this audit.
  Local upstream/master had three newer OpenSSL/logging commits unrelated
  to this pinned Zephyr baseline, so rebasing
  would have changed the comparison input without helping Milestone A.
- RMW: `servoagents/rmw_cyclonedds_c`
  `039e85860dc6032d51bd25dc552e5bdbbb0e686a`, followed by
  `patches/rmw-cyclonedds-c-fixed-waitset.patch` during target build.
- Kernel: Zephyr v4.4.0
  `684c9e8f32e4373a21098559f748f06915f950c9`. The accepted pre-fix
  runs used it unmodified. The common post-fix runs apply only
  `patches/zephyr-condvar-timeout-relock.patch`, copied exactly from Zephyr
  commit `5c6c6837cc4026a70e670c265fc4d1e3b1f16379`.
- ROS 2 Lyrical package revisions are pinned individually in
  `dependencies/target-lyrical.repos` and `dependencies/host-lyrical.repos`;
  the former is the source of the verified RMW SHA. Platform revisions are
  in `dependencies/platform.repos`. No dependency was silently updated.
- Host GCC 15.2.0, CMake 4.2.3, Zephyr SDK 1.0.1, native target
  `native_sim/native/64`, hardware target
  `esp32s3_devkitc/esp32s3/procpu`. The repaired existing Zephyr venv has
  `natsort==8.4.0`; `scripts/setup.sh` now installs the pinned Zephyr
  build-test and run-test requirement sets rather than relying on manual pip.
- The deployment is `samples/static_control/deployment.json`: domain 95,
  fixed Best Effort/Volatile command and state endpoints, 8 worker maximum,
  16,384 B separate application reserve, ESP limits 1,100,000 B flash and
  300,000 B linked RAM. Cyclone XML is the value in
  `cmake/rmw/cyclonedds_c.cmake` (loopback peer 127.0.0.1, multicast off,
  1,400 B max message, 8 KiB receive buffer); ESP static-control XML instead
  uses the current desktop IP `192.168.1.103`. Desktop peer in the Phase 3
  checkpoint was stock Lyrical `rmw_cyclonedds_cpp`. No desktop peer was run
  in this Milestone A qualification.

Build commands (from `ros2_zephyr`, with `build/zephyr-env-lyrical.sh`
already provisioned):

```sh
ROS2_ZEPHYR_NATIVE_BUILD_DIR="$PWD/build/phase4-a/baseline-loopback-native" ROS2_ZEPHYR_BUILD_JOBS=2 samples/loopback/build_native.sh
ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR="$PWD/build/phase4-a/baseline-static-native" ROS2_ZEPHYR_BUILD_JOBS=2 samples/static_control/build_native.sh
scripts/apply_zephyr_condvar_patch.sh ../build/zephyr-4.4.0/zephyr
ROS2_ZEPHYR_NATIVE_BUILD_DIR="$PWD/build/phase4-a/fixed-loopback-native" ROS2_ZEPHYR_BUILD_JOBS=2 samples/loopback/build_native.sh
ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR="$PWD/build/phase4-a/fixed-static-native" ROS2_ZEPHYR_BUILD_JOBS=2 samples/static_control/build_native.sh
source build/zephyr-env-lyrical.sh
west twister -T samples/condvar_qualification -p native_sim/native/64 --outdir build/phase4-a/condvar-after --inline-logs
west twister -T samples/static_control/tests/plant -T samples/static_control/tests/control -p native_sim/native/64 --outdir build/phase4-a/existing-twister --inline-logs
```

ESP build and board commands (credentials were sourced from the active
BlueHarbor NetworkManager profile, exported only into the build process and
never printed or committed; substitute your current Wi-Fi variables):

```sh
source build/zephyr-env-lyrical.sh
west twister -T samples/condvar_qualification -p esp32s3_devkitc/esp32s3/procpu --build-only --outdir build/phase4-a/condvar-esp-build --inline-logs
condvar_build=build/phase4-a/condvar-esp-build/esp32s3_devkitc_esp32s3_procpu/zephyr_gnu/ros2_zephyr/samples/condvar_qualification/ros2_zephyr.condvar_qualification
west flash -d "$condvar_build" --runner esp32 --no-rebuild --esp-device /dev/ttyUSB0
python samples/loopback/capture_esp32.py /dev/ttyUSB0 --timeout 20 --until 'PROJECT EXECUTION SUCCESSFUL'
ROS2_ZEPHYR_BOARD_OVERRIDE=esp32s3_devkitc/esp32s3/procpu ROS2_ZEPHYR_ESP32_BUILD_DIR="$PWD/build/phase4-a/fixed-loopback-esp32s3" ROS2_ZEPHYR_BUILD_JOBS=2 samples/loopback/build_esp32.sh
west flash -d build/phase4-a/fixed-loopback-esp32s3 --runner esp32 --no-rebuild --esp-device /dev/ttyUSB0
samples/loopback/run_esp32.sh /dev/ttyUSB0 build/phase4-a/loopback-hardware
# WIFI_SSID, WIFI_PSK and ROS_PEER_IP must be exported before this build.
ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR="$PWD/build/phase4-a/fixed-static-esp32s3" ROS2_ZEPHYR_BUILD_JOBS=2 samples/static_control/build_esp32.sh
west flash -d build/phase4-a/fixed-static-esp32s3 --runner esp32 --no-rebuild --esp-device /dev/ttyUSB0
python samples/loopback/capture_esp32.py /dev/ttyUSB0 --timeout 90 --until 'STATIC_CONTROL_METRICS' --minimum-matches 12
```

The test was also run before the Zephyr patch with
`--outdir build/phase4-a/condvar-before`. The `*.twister.log` files,
Twister JSON/XML and handler logs preserve the results. Each build directory
contains ELF, map, `.config`, CMake cache, generated headers and deployment
plan/report; `compile_commands.json` is present for fixed native static
control. Native has no ESP-style `zephyr.bin`; the executable is
`zephyr/zephyr.exe`. The `*-build.log` files preserve post-fix build output.
The qualification test also built for ESP32-S3 with Twister `--build-only`
(1/1 build, 0 executed) in `condvar-esp-build`, then was separately flashed
and executed through `/dev/ttyUSB0`; its full log is `condvar-esp-uart.log`.
The ESP loopback build and run logs are `fixed-loopback-esp32s3-build.log`
and `loopback-hardware/loopback.log`; the full static-control build, flash,
and failed startup logs are `fixed-static-esp32s3-{build,flash,run-1}.log`.
`capture_esp32.py --minimum-matches 12` can collect a complete metrics
window without changing the firmware, but none has yet been produced by the
full image.

## Qualification and fresh measurements

The pre-fix Twister test passed signal cases but failed direct and POSIX
finite, zero, and contended timeout ownership cases (2/8 pass). An independent
thread's non-blocking mutex acquisition proved the timed-out waiter did not
own the mutex. Under contention, the waiter returned near the deadline,
before the 70 ms owner released it. This is exactly the kernel regression,
not proof that native DDSRT would fix it. With the separately applied kernel
patch, the same 8/8 test cases pass. The Cyclone compatibility macro
`CYCLONEDDS_ZEPHYR_CONDVAR_TIMEOUT_RELOCK` is present in pre-fix compile
flags and absent in the fixed builds, avoiding a second lock/deadlock.
The fixed qualification image was also flashed to the physical ESP32-S3;
UART reports 8/8 pass, including both contended timeout cases. The small
test's generic 8 MB header produces a ROM flash-size warning on the actual
32 MB board, but the test executes successfully. It does not establish that
the full Wi-Fi image boots.

| Native image | Flash B | Linked RAM B | Notes |
|---|---:|---:|---|
| Supplied Phase 3 static-control checkpoint | 722,773 | 1,224,121 | Historical checkpoint, not rerun |
| Fresh pre-fix static-control | 722,863 | 1,224,121 | +90 B vs checkpoint; source/build provenance differs, not silently normalized |
| Fresh fixed-kernel static-control | 722,695 | 1,224,121 | -168 B vs same-worktree pre-fix; old Cyclone relock code removed |
| Fresh pre-fix loopback | 720,210 | 621,225 | Pass |
| Fresh fixed-kernel loopback | 720,042 | 621,225 | Pass |

The fixed static-control native image retains 159,495 B after its 16,384 B
reserve under the 1,400,000 B RAM limit. The loopback executable reports
`ROS2_ZEPHYR_LOOPBACK_PASS`, zero steady ROS allocation attempts and zero
live ROS/middleware allocations on cleanup. The existing plant/control
Twister suites pass 6/6; init-failure cleanup test passes.

Five fixed native static-control 9 s windows were captured in
`fixed-static-native-run-{1..5}.log` (8 one-second cumulative records each;
`timeout` status 124 is intentional). The existing metrics checker passes all
five: maximum adjacent middleware allocation deltas 29, 29, 21, 21, 29;
final cumulative attempts 224, 224, 157, 157, 224. All records have
`ros_steady_alloc_calls=0`, `skipped=0`, `max_lateness_us=0`, and 2,008 B
unused control stack. The 21/29 difference tracks background discovery/log
activity; it is not a reason to relax the existing at-most-32 quiescent gate.
The native report's 131,072 B Zephyr and 262,144 B libc heap backings are
already inside linked RAM; runtime high-water is not added to them.
The app emits these records from a one-second timer, but the present metrics
lines have no individual timestamps. Their spacing cannot be independently
validated from this capture; add timestamps in both comparison arms before
interpreting any future adjacent delta as exactly one second.

The supplied Phase 3 ESP static-control checkpoint remains 1,015,940 B
flash, 262,120 B linked DRAM, 1,664 B unused control stack, zero steady ROS
attempts, maximum middleware delta 25, no skipped release and successful
typed stock-peer interoperability. **Its runtime figures are historical, not
a fresh Milestone A run.** Fresh fixed-kernel static-control build/verification
passes at 1,015,528 B flash and 261,960 B linked DRAM. Against the historical
image figure these are -412 B flash and -160 B RAM, but the source/build
provenance is not identical enough to attribute that difference solely to the
kernel fix. Occupied RAM plus the separate 16,384 B reserve leaves 21,656 B
of the 300,000 B budget. The Cyclone timeout relock workaround is absent.

Fresh fixed-kernel ESP loopback builds at 712,548 B flash and 169,160 B
linked RAM and **runs on the board**: `ROS2_ZEPHYR_LOOPBACK_PASS`, zero steady
ROS allocation attempts, and zero live ROS/middleware allocations at cleanup.
The full static-control image flashes with verified hash, but its UART trace
ends after the ROM entry point, before the Zephyr boot banner. A pre-existing
full static-control image from before this kernel patch shows the same stall;
the small kernel test and the smaller loopback image both boot. An inadequate
power supply for the larger image is plausible, not proven. The only USB
device observed is the CP210x UART adapter; separate board 5 V power has
been requested. No static-control READY or metrics line was observed, so the
three planned 12-sample hardware windows and stock-peer interoperability
remain **not run**, not passed. Both failed startup captures are retained.

## Frozen comparison policy before native DDSRT work

Use the same pinned source, fixed-waitset patch, separately fixed Zephyr,
generated deployment, config, DDS XML, worker count, usable stacks, heap
backing, compiler flags, network and logging in both POSIX and experimental
native-sync images. Pristine build directories are mandatory. Preserve all
logs, plans, maps, ELFs and symbol/pool attribution. Do not lower capacities
in the controlled comparison; report any later dependency-reduced image
separately. A build-only result is not a run-pass.

For native static control, use five 9 s quiescent runs after READY; require at
least 8 consecutive timestamped one-second metrics records per run, zero ROS
steady allocation attempts, at most 32 new middleware attempts per adjacent
valid sample, zero skipped control releases, no fault, and at least 1,664 B
unused of the same 2,048 B control stack. Log all per-run values, not the best
run. The observed coarse-ms `max_lateness_us=0` is not a real-time bound.
Future matched timing comparison must instrument individual 10 ms releases
and report sample count, median, p95, p99 and maximum for release and
completion lateness; accept no new deadline miss or skipped release and no
more than 1 ms increase in p99 or maximum versus matched POSIX runs. This
1 ms provisional regression limit is the existing timer measurement quantum;
do not represent it as proven sub-ms precision. If instrumentation changes
the image, instrument both arms identically and rebaseline before comparison.

For hardware, use at least three matched quiescent windows of 12 metrics
samples plus the unchanged typed state/command peer test, ACTIVE/STALE expiry
at 250 ms, startup without peer, discovery churn and supported network
recovery. Keep ESP flash ≤1,100,000 B and occupied linked DRAM plus separate
16,384 B reserve ≤300,000 B. Require zero steady ROS attempts, the same
at-most-32 quiet-graph middleware delta, no skipped release/fault, and
≥1,664 B unused control stack unless a separately reviewed stack analysis
establishes a new minimum. Churn intervals are reported separately, never
used to weaken the quiescent gate. Record actual peer/RMW, IPs, build
configuration and UART log without committing network credentials.
