# ROS 2 on Zephyr

`ros2_zephyr` runs the standard ROS 2 C stack directly on Zephyr RTOS:

```text
Zephyr application
  -> rclc
  -> rcl
  -> selected RMW backend
  -> selected middleware
  -> Zephyr networking and kernel services
```

The accepted backend is `rmw_cyclonedds_c`, for which the device is a DDS/RTPS
participant. A second, experimental backend builds `rmw_zenoh_pico` directly
against Zenoh-Pico. Neither path uses a micro-ROS Agent.

## Start here

Use this project when an MCU should run ordinary ROS 2 C application code and
participate directly in DDS, with explicit deployment and memory limits.
The current application profile supports scalar, fixed-array and nested
fixed-size messages. Services, actions, generic variable-size messages and
DDS Security are outside the supported profile.

**Recommended experimental hardware path: ESP32-C6.** Start with the
[C6 application guide](docs/getting-started-c6.md): prepare the locked development
environment, copy the small pub/sub application, build it, inspect its resource
report, and connect it to a ROS 2 peer. The [A4 profile](docs/esp32c6-a4.md)
describes the underlying configuration and its limits.

### Evidence by target and image

“Builds”, “communicates”, “isolates compiler TLS” and “survives peer returns” are
separate claims. TLS here means **thread-local storage**, not Transport Layer
Security. Qualification belongs to an exact firmware image and workload.

| Target / profile | Build support | Hardware communication | Compiler TLS | Lifecycle qualification |
| --- | --- | --- | --- | --- |
| ESP32-C6, original frozen A4 Ping64 | Recorded build | 1,000/1,000 pilot echoes | Qualified configuration with RISC-V alignment repair | 20 peer returns, three boots, abrupt loss and error paths; finite exercised coverage |
| ESP32-C6, packaged A4 reconstruction | Clean-checkout cross-build and static admission passed | Not yet qualified for the rebuilt image | TLS-aware static admission; original runtime evidence is not transferred | Pending for this exact rebuilt image |
| ESP32-C6, editable pub/sub sample | Fresh-workspace build and static admission passed | Two ten-value functional peer runs passed | TLS-aware admission; no new standalone runtime TLS probe | No stack/cache or full lifecycle qualification |
| ESP32-S3, historical Cyclone profiles | Builds; loopback and Wi-Fi profiles exist | Recorded pub/sub, QoS and graph successes | **Isolation probe failed on the documented Xtensa configuration** | Earlier communication tests do not establish safe TLS or A4 lifecycle coverage |
| ESP32 original (`esp32/procpu`) | Cross-build target | No equivalent C6 campaign claimed | Not established here | Not established here |
| `native_sim/native/64` | Native loopback with Lyrical/Kilted | Simulation, not hardware evidence | Standalone isolation probe passed | Does not establish MCU lifecycle behavior |

Read the [C6 evidence and limits](docs/esp32c6-a4.md#evidence-and-limits),
[packaged release check](https://github.com/servoagents/ros2-embedded-benchmarks/blob/main/docs/release-check.md),
and [S3 TLS investigation](docs/compiler-tls.md) before selecting a target.
The S3 evidence remains useful historical data; it is not the recommended
compiler-TLS configuration.

The C6 reference application uses best-effort, volatile, keep-last depth 1,
with a fixed peer and bounded endpoints. Other middleware development profiles
have exercised Reliable/Volatile, Reliable/Transient Local and bounded graph
support. Those results do not extend the C6 sample's declared profile. See
[Wi-Fi baseline](docs/wifi-baseline.md), [graph acceptance](docs/graph-acceptance.md)
and [backend capabilities](docs/rmw-backends.md).

The experimental Zenoh-Pico backend has separate ESP32-S3 Volatile pub/sub
results in [its integration note](docs/zenoh-pico.md). It is not the C6 A4 path.
Exactly one RMW backend is selected and linked statically.

### What CI checks

The native job checks scripts and unit tests, prepares pinned dependencies, and
builds/runs native loopback. The C6 job prepares the locked development environment,
cross-builds the editable sample with dummy credentials and runs static resource
admission. Neither job flashes a board or establishes runtime stack margins,
Wi-Fi interoperability or lifecycle qualification. Workflow results apply to
the revision actually run; adding a job is not evidence that hosted CI passed.

The separate benchmark repository's host checks validate accounting, archived
evidence and the reconstructed Cyclone regressions. Its historical hardware
records are not hardware CI.

See [project status](ROADMAP.md), [resource contracts](docs/resource-contracts.md),
[deployment generation](docs/deployment-compiler.md), and [changes](CHANGELOG.md).

## Earlier targets and native simulation

The commands below use the older general setup path. For ESP32-C6, follow the
[C6 application guide](docs/getting-started-c6.md) and its separate locks.

### Prerequisites

- Linux
- Git, CMake, Ninja, and Python 3.12 or newer
- enough disk space for a Zephyr workspace, the SDK, and pinned ROS sources
- a supported ESP32 or ESP32-S3 board for the hardware sample

The setup script creates an isolated Python environment and keeps downloaded
sources and build output below the ignored `build/` directory.

### Native quick start

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

Enable the module and exactly one backend in an application configuration:

```text
CONFIG_ROS2_ZEPHYR=y
CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C=y
```

or:

```text
CONFIG_ROS2_ZEPHYR=y
CONFIG_ROS2_ZEPHYR_RMW_ZENOH_PICO=y
```

The selected Cyclone backend requires paths to the prepared dependency tree,
the pinned Cyclone DDS checkout, and the host `idlc` executable. The supplied
setup and sample scripts provide those paths. See
[the architecture notes](docs/architecture.md) for the portability boundary
and source provenance.

Lyrical adds native-buffer APIs implemented in C++. This fixed C profile builds
the ordinary ROS C sequences and introspection data, but disables native-buffer
ownership and uses `rcl_logging_noop` instead of the dynamic C++ logging loader.
Buffer-annotated fields remain unsupported.

### Resource sizing

Generated deployments declare target-specific resource contracts. CMake
rejects endpoint, discovery, worker, and known-RAM overcommit during
configuration, then verifies the linked ELF and map against the declared flash
and RAM budgets. The generated reports keep planned, linked, and runtime
measurements separate. See [resource admission and post-link
verification](docs/resource-contracts.md).

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

The experimental Zenoh backend uses
[`fj-blanco/rmw_zenoh_pico`](https://github.com/fj-blanco/rmw_zenoh_pico),
Zenoh-Pico, and Micro-CDR at exact GitHub revisions in the Lyrical target and
platform manifests. Its Volatile pub/sub hardware matrix and resource
comparison are documented in [the Zenoh-Pico integration note](docs/zenoh-pico.md).

## License

Apache License 2.0. See [LICENSE](LICENSE).

ROS 2, Zephyr, and Eclipse Cyclone DDS are trademarks of their respective
owners. This project is not endorsed by Open Robotics, the Zephyr Project, or
the Eclipse Foundation.
