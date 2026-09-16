# ESP32-S3 Wi-Fi interoperability sample

This sample runs a normal `rclc` node on an ESP32-S3 and exchanges
`std_msgs/msg/UInt32` messages with an unmodified ROS 2 desktop node. Cyclone
DDS communicates directly over Wi-Fi; no Agent is involved.

The test uses best-effort QoS and sends 18 messages: three at 1 Hz, five at
10 Hz, and ten at 100 Hz. Run both directions by swapping the device and
desktop roles.

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

The supplied board overlay targets an ESP32-S3-DevKitC with 32 MiB flash and
16 MiB octal PSRAM. Adapt the overlay before using a different module.

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

## Resource profile

The Wi-Fi configuration reserves 8 KiB per Cyclone worker and 256 Zephyr
POSIX mutex slots. A 192-slot pool was exhausted while the full ROS node was
processing a stock desktop peer's discovery endpoints. The loopback sample's
smaller settings are not suitable for this test.

Stack fill was inspected after successful exchanges in both directions. The
following figures are high-water marks for this test, not worst-case bounds:

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
