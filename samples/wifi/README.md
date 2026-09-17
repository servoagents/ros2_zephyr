# ESP32-S3 Wi-Fi interoperability sample

This sample runs a normal `rclc` node on an ESP32-S3 and exchanges
`std_msgs/msg/UInt32` messages with an unmodified ROS 2 desktop node. Cyclone
DDS communicates directly over Wi-Fi; no Agent is involved.

The test sends 18 messages: three at 1 Hz, five at 10 Hz, and ten at 100 Hz.
Best effort is the default and preserves the accepted hardware regression;
Reliable can be selected explicitly. Run both directions by swapping the
device and desktop roles.

## Configuration

Prepare the pinned Lyrical workspace first:

```sh
scripts/setup.sh
```

Store the Wi-Fi credentials and desktop IPv4 address in the ignored build
directory:

```sh
install -m 600 samples/wifi/wifi.env.example build/wifi.env
```

Edit `build/wifi.env`. Do not commit this file or paste its contents into test
logs. The generated credential header also remains below `build/`.

The supplied board files target an ESP32-S3-DevKitC module with 32 MiB octal
flash and 16 MiB octal PSRAM. Adapt both the devicetree overlay and Kconfig
fragment before using a different module. The board fragment selects octal STR
flash mode; DTR calibration does not complete on the tested 32 MiB MXIC part.

After flashing through the native USB Serial/JTAG port, disconnect and
reconnect board power before running the peer. A USB reset alone did not start
the application reliably on the tested board.

## Device subscriber

Build and flash the subscriber through either the UART bridge or the native
USB Serial/JTAG port. Use the device name that appears on the host:

```sh
samples/wifi/run_esp32.sh sub /dev/ttyACM0
```

The UART bridge commonly appears as `/dev/ttyUSB0`.

In a ROS 2 Lyrical shell on the desktop, run:

```sh
export ROS_DOMAIN_ID=91
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
python3 samples/wifi/peer.py pub
```

The firmware succeeds after it receives all 18 expected values and releases
its ROS and DDS allocations. Because this profile is best-effort, the desktop
peer sends up to ten recovery samples if the scheduled sweep loses a packet.
It reports success only after the device endpoint disappears during cleanup.

For Reliable, add the policy to both commands:

```sh
samples/wifi/run_esp32.sh sub /dev/ttyACM0 reliable
python3 samples/wifi/peer.py pub --reliability reliable
```

The Reliable desktop publisher does not send recovery samples. Completion
therefore depends on DDS retransmission.

## Device publisher

Start the desktop subscriber before resetting or flashing the board:

```sh
export ROS_DOMAIN_ID=91
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
python3 samples/wifi/peer.py sub
```

Then build and flash the publisher in another shell:

```sh
samples/wifi/run_esp32.sh pub /dev/ttyACM0
```

The desktop peer fails unless it receives all 18 expected values. It prints
arrival intervals for diagnostic use; those intervals are not a network
latency measurement.

The Reliable form is:

```sh
python3 samples/wifi/peer.py sub --reliability reliable
samples/wifi/run_esp32.sh pub /dev/ttyACM0 reliable
```

## Development middleware source

The dependency manifest points at a committed middleware revision. To
cross-build uncommitted middleware work from the sibling repository, set:

```sh
export ROS2_ZEPHYR_RMW_SOURCE="$(realpath ../rmw_cyclonedds_c)"
samples/wifi/build_esp32.sh sub reliable
samples/wifi/build_esp32.sh pub reliable
```

The source is copied into the isolated target workspace without its `.git`
directory or test results.

## Reliable acceptance status

Best effort has passed on the physical ESP32-S3 in both directions. Reliable
also passes in both physical directions on Zephyr 4.4.2 against the stock
Lyrical `rmw_cyclonedds_cpp` peer. The device subscriber accepted the complete
18-sample schedule; the desktop publisher used no recovery samples. The
desktop subscriber received all 18 samples from the device publisher.

The accepted Reliable subscriber uses 1,016,916 bytes of flash and 258,088
bytes of linked DRAM. The publisher uses 940,148 bytes of flash and 258,080
bytes of linked DRAM. The native USB capture retained the ROM and simple-boot
output but not the application console after handoff, so these runs do not
provide allocator or stack high-water marks.

## Resource profile

The Wi-Fi configuration reserves 8 KiB per Cyclone worker and 256 Zephyr
POSIX mutex slots. A 192-slot pool was exhausted while the full ROS node was
processing a stock desktop peer's discovery endpoints. The loopback sample's
smaller settings are not suitable for this test.

Stack fill was inspected after the accepted Best Effort exchanges in both
directions. The following figures are high-water marks for that test, not
worst-case bounds or Reliable measurements:

| Thread | Reserved | Device subscriber used | Device publisher used |
| --- | ---: | ---: | ---: |
| `recv` | 8,192 B | 3,984 B | 1,792 B |
| `tev` | 8,192 B | 3,584 B | 3,408 B |
| `dq.user` | 8,192 B | 528 B | 528 B |
| `dq.builtins` | 8,192 B | 7,696 B | 7,696 B |
| `gc` | 8,192 B | 4,096 B | 4,096 B |
| application `main` | 12,288 B | 11,312 B | 11,264 B |

The complete image had 12 threads and reserved 70,144 stack bytes. The
`dq.builtins` worker had only 496 bytes unused, so the 8 KiB worker setting
should not be reduced on the strength of this measurement.

Upstream `rclc` links its action support into the executor library and declares
`rcl_action` as a required dependency. The executor retains references to that
code even in this pub/sub-only sample. On ESP32, resolving the action UUID
helper also pulls Picolibc's `random()` into the link, where it conflicts with
the ESP32 Wi-Fi adapter's function of the same name. The module therefore
provides `rand()` and `srand()` only for ESP32 Wi-Fi builds. This compatibility
shim does not add action support to the current profile.

The current workaround for Zephyr's timed condition-wait relock bug is enabled
only for the 4.4.0 through 4.4.2 release tags. It should be removed once the
project moves to a stable Zephyr release containing the upstream fix.
