# ROS 2 on Zephyr

`ros2_zephyr` runs the standard ROS 2 C stack directly on Zephyr RTOS:

```text
Zephyr application
  -> rclc
  -> rcl
  -> rmw_cyclonedds_c
  -> Eclipse Cyclone DDS
  -> Zephyr networking and kernel services
```

The device is a DDS/RTPS participant. This path does not use Micro XRCE-DDS or
a micro-ROS Agent.

## Status

The current release is an early, fixed-profile port:

- `native_sim/native/64`, `esp32_devkitc/esp32/procpu`, and
  `esp32s3_devkitc/esp32s3/procpu` are supported build targets;
- the loopback sample runs through ordinary `rclc` and `rcl` APIs;
- scalar, fixed-array, and nested fixed-size messages are supported;
- Cyclone DDS supports best-effort or reliable, volatile, keep-last QoS;
- ROS 2 and Cyclone DDS are linked into one static archive.

The loopback sample has passed on an ESP32-S3-DevKitC using ROS 2 Lyrical. The
same test also passes on `native_sim` with Lyrical and Kilted. On Zephyr 4.4.2,
the [Wi-Fi sample](samples/wifi/README.md) exchanged `std_msgs/msg/UInt32`
messages in both directions with an unmodified Lyrical desktop node at 1, 10,
and 100 Hz. The test used `rclc`, `rcl`, `rmw_cyclonedds_c`, and Cyclone DDS on
the board, with no Agent.

The accepted Wi-Fi result used best-effort QoS. Reliable QoS passes the Linux
interop and retransmission lanes and cross-builds for both ESP32-S3 roles, but
has not yet completed physical hardware acceptance. The repository's pinned
Zephyr baseline remains 4.4.0 until a stable 4.4.x release contains the timed
condition-wait fix. Services, actions, transient-local durability,
variable-size messages, DDS Security, and a complete remote graph are outside
the current profile.

The exact accepted hardware configuration, including its XTypes boundary, is
recorded in [the Wi-Fi baseline](docs/wifi-baseline.md).

## Prerequisites

- Linux
- Git, CMake, Ninja, and Python 3.12 or newer
- enough disk space for a Zephyr workspace, the SDK, and pinned ROS sources
- a supported ESP32 or ESP32-S3 board for the hardware sample

The setup script creates an isolated Python environment and keeps downloaded
sources and build output below the ignored `build/` directory.

## Quick start

```sh
scripts/setup.sh
scripts/run.sh native
```

The first command downloads the pinned Zephyr, ROS 2, middleware, and Cyclone
DDS revisions and builds the host `idlc` tool. Later builds verify those
revisions and can run offline.

Lyrical is the default ROS distribution. Kilted remains available in a
separate dependency and build tree:

```sh
ROS2_ZEPHYR_ROS_DISTRO=kilted scripts/setup.sh
ROS2_ZEPHYR_ROS_DISTRO=kilted scripts/run.sh native
```

Cross-build the same application path for ESP32:

```sh
scripts/run.sh esp32
```

With a supported board attached:

```sh
scripts/run.sh run-esp32 /dev/ttyUSB0
```

For an ESP32-S3-DevKitC, select the S3 target and a distinct build directory:

```sh
export ROS2_ZEPHYR_BOARD_OVERRIDE=esp32s3_devkitc/esp32s3/procpu
export ROS2_ZEPHYR_ESP32_BUILD_DIR="$PWD/build/lyrical/esp32s3"
scripts/run.sh run-esp32 /dev/ttyUSB0
```

The S3 overlay is configured for the tested 32 MiB flash and 16 MiB octal
PSRAM module. Adjust it before building a board with different memory.

For direct Wi-Fi interoperability with a desktop ROS 2 node, use the separate
[ESP32-S3 Wi-Fi sample](samples/wifi/README.md).

Builds use one job by default to limit peak memory. Set
`ROS2_ZEPHYR_BUILD_JOBS` to opt into parallel builds.

## Zephyr module use

Enable the module in an application configuration:

```text
CONFIG_ROS2_ZEPHYR=y
CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C=y
```

The module requires paths to the prepared dependency tree, the pinned Cyclone
DDS checkout, and the host `idlc` executable. The supplied setup and sample
scripts provide those paths. See [the architecture notes](docs/architecture.md)
for the portability boundary and source provenance.

Lyrical adds native-buffer APIs implemented in C++. This fixed C profile builds
the ordinary ROS C sequences and introspection data, but disables native-buffer
ownership and uses `rcl_logging_noop` instead of the dynamic C++ logging loader.
Buffer-annotated fields remain unsupported.

### Resource sizing

Embedded builds reserve 8 KiB for each Cyclone DDS worker by default. During a
direct-DDS ESP32-S3 test, external discovery used about 6 KiB while creating
builtin proxy endpoints; the earlier 4,864-byte allocation overflowed. The
loopback sample keeps its smaller measured allocation because it has no
external peer.

POSIX mutexes are configured by the application. The direct-DDS fixture passed
with a 192-slot pool, but the complete `rclc` node exhausted that pool while
processing a stock desktop peer's discovery endpoints. The tested Wi-Fi sample
uses:

```text
CONFIG_MAX_PTHREAD_MUTEX_COUNT=256
```

The loopback sample's 160-slot setting is specific to its local workload and is
not sufficient for external DDS discovery. The Wi-Fi sample keeps 96 condition
variables; the direct-DDS measurement peaked at 30.

In the accepted Zephyr 4.4.2 builds, the subscriber used 1,016,132 bytes of
flash and 258,072 of 399,108 available DRAM bytes. Its tracked ROS allocator
peaked at 917 bytes and its tracked DDS allocator at 98,105 bytes. The
publisher used 939,508 bytes of flash and 258,064 bytes of DRAM. These figures
describe this sample and toolchain, not general minimum requirements.

The compile-only Reliable images use 1,016,404 bytes of flash and 258,072
bytes of linked DRAM for the subscriber, and 939,748 bytes of flash and
258,064 bytes of linked DRAM for the publisher. Allocator and stack high-water
marks require a physical Reliable run and are not inferred from the link map.

## Middleware

The RMW and generated type support live in
[`servoagents/rmw_cyclonedds_c`](https://github.com/servoagents/rmw_cyclonedds_c).
This repository pins an exact middleware commit in each distribution-specific
target manifest under `dependencies/`.

## License

Apache License 2.0. See [LICENSE](LICENSE).

ROS 2, Zephyr, and Eclipse Cyclone DDS are trademarks of their respective
owners. This project is not endorsed by Open Robotics, the Zephyr Project, or
the Eclipse Foundation.
