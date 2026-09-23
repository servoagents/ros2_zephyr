# Prototype results

Measured on 22 September 2026. These results apply to the configurations below,
not to arbitrary ROS 2 or Zephyr applications.

## Revisions and setup

- Baseline `ros2_zephyr`: `f35adf1ee55f1fe78bef0687750b190505e4e857`
  (`v0.1.0-alpha.2`)
- Candidate `ros2_zephyr`: `d7f7bf44acd68e68dd1ac4363594402f2f32ac1d`
  plus the changes in this worktree
- Zephyr 4.4.0; Zephyr SDK 1.0.1
- Cyclone DDS: `2f0d07d241f62f7121749b46721049e4dea5c58b`
- RMW: the pinned Lyrical target source; no change was made to the separate
  `rmw_cyclonedds_c` worktree at
  `eaf8040f0cfbe729ad2bf6cbb38ce32af7b9e0fe`
- Hardware: ESP32-S3 revision 0.2, 16 MiB PSRAM, USB Serial/JTAG
- Control peer: ROS 2 Humble at `192.168.0.16`, domain 95, Cyclone DDS
- QoS and graph peer: stock ROS 2 Kilted in
  `ros2-zephyr-kilted-peer:phase1`
- Control endpoints: Best Effort, Volatile, depth one

Build directories contain the ELF, map, effective `.config` and build logs.
Generated Wi-Fi credentials remain under ignored build directories.

## Existing loopback sample

The same native loopback application and nested fixed message ran before and
after the change. The candidate explicitly prepares the executor and reduces
native-only fallback reservations. Three runs of each image produced identical
results.

| Measure | Baseline | Candidate | Change |
|---|---:|---:|---:|
| ELF text | 687,492 B | 687,589 B | +97 B |
| ELF data | 54,380 B | 54,380 B | 0 B |
| ELF bss | 9,953,093 B | 567,357 B | -9,385,736 B |
| ELF total | 10,694,965 B | 1,309,326 B | -9,385,639 B |
| Reserved stacks | 473,312 B | 62,688 B | -410,624 B |
| ROS setup allocation calls | 22 | 29 | +7 |
| ROS steady allocation calls | 7 | 0 | -7 |
| DDS setup allocation calls | 739 | 740 | +1 |
| DDS steady allocation calls | 19 | 18 | -1 |
| ROS high-water occupancy | 1,775 B | 1,775 B | 0 B |
| DDS high-water occupancy | 85,778 B | 85,778 B | 0 B |

The `.bss` reduction comes from named reservations, principally the libc arena
(8 MiB to 256 KiB), Zephyr heap (512 KiB to 128 KiB), main and DDS stacks, and
network packet/buffer counts. The measured allocation high-water mark is below
86 KiB; the candidate keeps a 256 KiB libc arena. This is a native simulation
result. The ESP32 configuration was already separately bounded and these bytes
must not be reported as an ESP32 saving.

Explicit executor preparation moves seven ROS allocation attempts from the
first spin into setup. It does not make Cyclone DDS allocation-free: its steady
counter still advances. Cleanup ended with zero live tracked ROS and DDS
allocations in every run.

Raw logs are in `build/measurements/loopback-{baseline,candidate}-{1,2,3}.log`.
Each run sends and verifies one nested fixed message. The images are under
`build/baseline-native` and `build/candidate-native`.

Section garbage collection and function/data section options were already in
the accepted build. No accidental archive retention was found, so no linker
change was kept. LTO was not tested.

## Static control sample

The new application has a 100 Hz preemptive Zephyr control thread and a
separate ROS-owning thread. The control path uses two short spinlock-protected
snapshots and typed fixed-size messages. It contains no ROS, network or heap
calls; unresolved-symbol inspection of `control.c` confirms only Zephyr kernel,
plant and compiler support calls.

Final image sizes:

| Target | text | data | bss | total |
|---|---:|---:|---:|---:|
| native_sim | 688,306 B | 53,623 B | 1,169,410 B | 1,911,339 B |
| ESP32-S3 ELF | 907,860 B | 29,592 B | 998,868 B | 1,936,320 B |

Zephyr's ESP32 region report for the final image is:

| Region | Used |
|---|---:|
| FLASH | 1,017,912 B |
| IRAM | 53,636 B |
| DRAM | 261,272 B |
| IROM | 687,500 B |
| DROM | 886,840 B |

These are absolute control-application sizes. They are not compared with the
smaller UInt32 loopback sample and are not an optimization claim. A matched
baseline-library build is unavailable because the baseline tag predates the
control sample and its message interfaces.

### Hardware behavior

The ESP32-S3 accepted 1,459 commands during a 30 second, 50 Hz stimulus. It
entered active state, expired the command after traffic stopped, disabled
effort, and returned to active state after a fresh command. Out-of-range input
increased the rejected count without changing the accepted sequence.

The 2,048-byte control stack retained 1,664 unused bytes, so observed use was
384 bytes. ROS steady allocation attempts remained zero after executor
preparation. DDS allocation attempts continued at about 120 per second; its
high-water occupancy reached 97,308 bytes during the 50 Hz run. This is why the
claim is limited to the application control path and prepared `rclc` executor.

The first long run exposed four skipped releases and 45 ms maximum lateness.
The ESP32 default main-thread priority was zero, so the ROS-owning main thread
could preempt the priority-three control thread. The application now selects
main-thread priority four and rejects higher-priority configurations at build
time. Three 180 second, 50 Hz runs then completed with no skipped releases and
no measured lateness at the one-millisecond timer resolution:

| Run | Accepted commands | Skipped releases | Maximum lateness |
|---|---:|---:|---:|
| 1 | 8,955 | 0 | 0 us |
| 2 | 8,999 | 0 | 0 us |
| 3 | 8,330 | 0 | 0 us |

The Best Effort data path lost part of the third stimulus and correctly became
stale; the local control schedule continued without a fault.

The ESP32 configuration now leaves Wi-Fi association and DHCP readiness to the
application instead of also running Zephyr's 30 second automatic network-init
wait before `main()`. In the final USB capture the startup marker appeared
immediately after the Zephyr banner, and the board reported network and ROS
readiness after about 4.9 seconds. The previous configuration did not enter
`main()` until about 35 seconds after boot.

The raw failing capture is `build/measurements/hardware-final.log`. Fixed-run
captures are `build/measurements/hardware-priority-fix.log`,
`build/measurements/hardware-priority-run2-active.log` and
`build/measurements/hardware-priority-run3.log`.

### Behavior matrix

| Case | Result | Evidence |
|---|---|---|
| No-command startup | Pass | Output starts disabled on hardware and in ztest |
| Valid command and state | Pass | ROS over Wi-Fi; plant response observed |
| 250 ms expiry | Pass | Hardware changed from active to stale with zero effort |
| Invalid input | Pass | Hardware rejected target 2.0; ztest covers non-finite/range checks |
| Command burst | Pass | ztest submits 1,000 commands; latest valid snapshot wins |
| Injected overrun | Pass | ztest latches fault and counts skipped releases |
| Peer loss and fresh-command recovery | Pass | Stale policy held locally; fresh sequence restored active state |
| Physical Wi-Fi disconnect/reconnect | Not run | Peer traffic stopped, but the access point link was not forced down |
| Invalid DDS worker capacity | Pass | Values below five are rejected by Kconfig before build |
| Initialization heap exhaustion | Fail | Cyclone aborts before `rcl` can return an error |
| Existing nested fixed loopback | Pass | Three baseline and three candidate runs |
| QoS matrix | Pass | Eight ESP32-S3 pub/sub cases against stock ROS 2 Kilted |
| Outbound graph lifecycle | Pass | Two create/remove lifecycles and desktop CLI discovery |
| Inbound graph lifecycle | Pass | Discovery, participant loss, restart and second loss |
| ESP target build and flash | Pass | Final image hash verified by `esptool` |

Plant ztest: 3 passed. Control-policy ztest: 3 passed. The latter covers burst,
expiry, invalid input and injected overrun using the real control thread.

QoS logs are under `build/measurements/wifi-current-qos-kilted/`. Graph logs
are under `build/measurements/wifi-current-graph-kilted/`. A Humble graph peer
was also tried but is not an accepted comparison: its graph message uses a
24-byte GID while the Lyrical target uses 16 bytes. Data-topic communication
with Humble remains supported and was used for the control stimulus.

### Known issues

Cyclone/Zephyr startup prints two existing filesystem mount errors and five
`tid ... is in use` messages while DDS workers continue to run. The ESP32 Wi-Fi
driver also reported two transient net-buffer allocation failures on some
boots. The application recovered, but these warnings need upstream ownership
work before stronger robustness claims.

Cyclone's mandatory allocation path calls `abort()` on heap exhaustion. Its
thread-creation failure path is also fatal, so configurations below the five
mandatory workers are now rejected by Kconfig. Unexpected runtime resource
failures still require error propagation inside Cyclone and were not hidden
behind sample-specific fault injection. Repeated DDS allocations remain the
next measured runtime cost; they were not changed without a safe ownership
model.

## Retained changes

- Explicit `rclc_executor_prepare()` in fixed endpoint samples
- Reduced native loopback reservations with a three-times allocation margin
- Fixed command/state interfaces with direct generated Cyclone typesupport
- A normal Zephyr control application with bounded snapshots, expiry and a
  latched overrun fault
- Control priority above the ROS-owning thread, enforced at build time
- Build-time rejection of insufficient Cyclone worker capacity
- Native plant and control-policy tests
- Immediate ESP32 application startup with one explicit Wi-Fi/DHCP owner
- ESP32-S3 build, board configuration and reproducible resource reporting

No scheduler, deployment language, compiler layer, runtime registry or generic
channel subsystem was added.

## Reproduction

```sh
scripts/check.sh
build/static-control-plant-test/zephyr/zephyr.exe
build/test-static-control/zephyr/zephyr.exe

samples/static_control/build_native.sh
build/lyrical/static-control-native/zephyr/zephyr.exe

export WIFI_SSID='your-network'
export WIFI_PSK='your-password'
export ROS_PEER_IP=192.168.0.16
samples/static_control/build_esp32.sh
west flash -d build/lyrical/static-control-esp32s3

samples/wifi/run_hardware_matrix.sh --device /dev/ttyACM0 \
  --rmw cyclonedds_c \
  --peer-container ros2-zephyr-kilted-peer:phase1 \
  --peer-address 192.168.0.16
```

On the peer, build `shims/ros2_zephyr_test_msgs`, set `ROS_DOMAIN_ID=95`, then
use the commands in `samples/static_control/README.md`.
