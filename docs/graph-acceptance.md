# ESP32-S3 ROS graph acceptance

This lane moves the bounded ROS graph implementation from its Linux
interoperability tests to the existing ESP32-S3 Wi-Fi sample. It does not add
a second graph protocol or a separate firmware application.

## Embedded bounds

The Zephyr module forwards five Kconfig values to `rmw_cyclonedds_c`:

| Resource | Wi-Fi default | Kconfig setting |
| --- | ---: | --- |
| Local nodes | 4 | `CONFIG_ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES` |
| Readers or writers per node | 8 | `CONFIG_ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE` |
| Cached participants | 4 | `CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS` |
| Cached nodes | 8 | `CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES` |
| Cached DDS endpoints | 24 | `CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS` |

These are fixed-capacity allocations made with the RCL allocator when the RMW
context is initialized. On the 32-bit ESP32-S3 ABI, the retained inbound cache
arrays occupy 19,560 bytes: 64 bytes for participant GUIDs, 6,368 bytes for
nodes, and 13,128 bytes for endpoints. The three counters, cache lock, and
padding before the following allocator add 16 bytes, for 19,576 bytes of fixed
cache state. These sizes were checked with the ESP32-S3 cross compiler. This does
not include outbound graph state or Cyclone's transient deserialization and
discovery allocations; the runtime allocator high-water marks include those.

The firmware prints the effective limits as `ROS2_ZEPHYR_GRAPH_LIMITS`. An
overflow remains an explicit `RMW_CYCLONEDDS_C_GRAPH_LIMIT` diagnostic and
does not truncate or replace the last accepted state.

## Build acceptance

Build all four requested topologies from the development middleware source:

```sh
export ROS2_ZEPHYR_RMW_SOURCE="$(realpath ../rmw_cyclonedds_c)"
samples/wifi/build_graph_matrix.sh
```

The matrix produces `node`, `pub`, `sub`, and `pubsub` ESP32-S3 images and
writes per-role logs plus a summary below
`results/lyrical/graph-esp32s3-build/`. The `pub` and `sub` images retain the
existing data-interoperability behavior. `pubsub` is a graph lifecycle image;
`node` is the inbound graph diagnostic.

## Outbound graph lifecycle

Build and flash the `pubsub` image, and start the stock Lyrical peer before
the board boots so it observes every transition:

```sh
python3 samples/wifi/peer.py graph-outbound
```

```sh
ROS2_ZEPHYR_WIFI_BUILD_DIR=build/lyrical/wifi-esp32s3-graph-pubsub \
  samples/wifi/run_esp32.sh --role pubsub --device /dev/ttyACM0 \
    --reliability reliable --no-build
```

The desktop peer checks this sequence through stock `rclpy` graph APIs:

```text
/ros2_zephyr_esp32s3 with one publisher and one subscription
publisher removed while the subscription remains
both endpoints removed while the node remains
node removed during RMW cleanup
```

During the first phase, the same state can be inspected manually with:

```sh
ros2 node list --no-daemon
ros2 node info /ros2_zephyr_esp32s3 --no-daemon
ros2 topic list --no-daemon
ros2 topic info /ros2_zephyr/graph_local --verbose --no-daemon
```

## Inbound graph lifecycle

Start the stock peer, then flash the already-built `node` image:

```sh
python3 samples/wifi/peer.py graph-inbound
```

In another terminal:

```sh
ROS2_ZEPHYR_WIFI_BUILD_DIR=build/lyrical/wifi-esp32s3-graph-node \
  samples/wifi/run_esp32.sh --role node --device /dev/ttyACM0 \
    --reliability reliable --no-build
```

The stock peer launches two namespaced nodes in separate processes and DDS
participants, with two publishers and two subscriptions on one topic and a
publisher on a second topic. It reduces the first node's topology and kills the
second process, waits for lease-based participant cleanup, kills the remaining
process, restarts the same node name on a new participant with one subscription,
and kills it again. Process loss exercises the participant-loss path without an
orderly DDS teardown burst. Separate initial participants also avoid Lyrical's
documented stale graph snapshot when one of several nodes sharing a participant
is destroyed; the embedded cache does not infer that node's removal from
endpoint changes.

The board queries the cache through `rcl_get_node_names()`,
`rcl_get_topic_names_and_types()`, `rcl_count_publishers()`, and
`rcl_count_subscribers()`. Each accepted phase prints
`ROS2_ZEPHYR_GRAPH_PASS`; all five phases must pass before the firmware reports
successful cleanup. After the largest phase is verified, the firmware prints
`ROS2_ZEPHYR_GRAPH_CACHE_EVIDENCE` for the validated topology: the local node
and two remote nodes across three participants, with five remote endpoints. This
is topology-derived evidence rather than access to private RMW counters. The
peer uses ordinary ROS graph announcements and DDS discovery only.

## Automated physical matrix

The complete physical run is automated from a stock ROS 2 Lyrical shell:

```sh
export ROS_DOMAIN_ID=91
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
samples/wifi/run_hardware_matrix.sh --device /dev/ttyACM0
```

If the host has no Lyrical installation, run the stock peer and CLI through the
existing acceptance container while keeping build, flash, and UART access on
the host. The runner reads `ROS_PEER_IP` from the ignored Wi-Fi environment and
binds desktop Cyclone DDS to that address so Docker and bridge interfaces are
not selected accidentally:

```sh
samples/wifi/run_hardware_matrix.sh --device /dev/ttyACM0 \
  --peer-container servoagents/rmw-cyclonedds-c:lyrical
```

The runner builds from the sibling `rmw_cyclonedds_c` checkout, flashes and
captures each image, starts the matching stock `rclpy` peer, and writes isolated
logs below `results/lyrical/esp32s3-hardware/`. It runs outbound and inbound
graph acceptance followed by both data directions for these profiles:

| Reliability | Durability | Depth |
| --- | --- | ---: |
| Best Effort | Volatile | 5 |
| Reliable | Volatile | 5 |
| Reliable | Transient Local | 1 |
| Reliable | Transient Local | 3 |

The outbound graph case also records normal `ros2 node` and `ros2 topic`
output while both endpoints exist. It resets the board for a second lifecycle
while retaining the same desktop observer, which verifies disappearance and
rediscovery without stale state. A case passes only if the stock peer, UART
markers, resource markers, cleanup, and (for outbound graph) CLI snapshot all
pass. `--graph-only`, `--qos-only`, and `--no-build` allow safe reruns of part
of the matrix. Use `--no-reset` when observing a manual power cycle through a
native USB console; power-cycle after flashing completes and again after the
first cleanup while the capture is waiting.

For a release run, use `--build-root` with an empty directory. This prevents a
cached Zephyr source or toolchain selection from an earlier build from being
reused. Each case summary records the ROS 2 Zephyr, middleware, and Zephyr
revisions; a modified source tree is marked `-dirty`.

The capture helper reopens the serial device after native USB
disconnect/re-enumeration. On the tested board, the flash runner's USB reset
reaches the simple bootloader but does not reliably start the application; use
`--no-reset` and press the physical RESET/EN button (or remove and restore board
power) after each flash. The automated peer can also be given a longer window
with `run_hardware_case.sh --peer-timeout 240 --capture-timeout 300` when a
manual reset is required.

After an image has been flashed successfully, `--no-build --no-flash
--no-reset` arms the peer and reconnecting UART before a physical RESET/EN
press without issuing another USB reset.

To diagnose or repeat one case, use the lower-level runner directly:

```sh
samples/wifi/run_hardware_case.sh --role node --device /dev/ttyACM0 \
  --reliability reliable --durability volatile --depth 5 --no-build
```

## Resource evidence

Every firmware role reports linked flash/DRAM at build time. At runtime it
prints Zephyr heap peaks, ROS and middleware allocator high-water marks, thread
count, reserved stack, and per-thread unused stack. Record the `node` figures
after the complete inbound lifecycle because that run covers cache growth and
Cyclone's graph-sample deserialization. The automated case summary retains the
linked-memory totals and source revisions; `device.log` retains the runtime
measurements and graph cache topology evidence.

The clean post-refactor Lyrical/Zephyr 4.4.0 hardware matrix on 2026-09-21
produced:

| Role | QoS | Depth | Linked flash | Linked DRAM |
| --- | --- | ---: | ---: | ---: |
| `node` | Reliable/Volatile | 5 | 1,006,260 B | 260,840 B |
| `pubsub` | Reliable/Volatile | 5 | 1,011,276 B | 261,368 B |
| `pub` | Best Effort/Volatile | 5 | 1,010,756 B | 261,368 B |
| `sub` | Best Effort/Volatile | 5 | 1,019,476 B | 261,384 B |
| `pub` | Reliable/Volatile | 5 | 1,010,644 B | 261,368 B |
| `sub` | Reliable/Volatile | 5 | 1,019,320 B | 261,384 B |
| `pub` | Reliable/Transient Local | 1 or 3 | 1,010,924 B | 261,368 B |
| `sub` | Reliable/Transient Local | 1 or 3 | 1,019,676 B | 261,384 B |

These are the exact accepted values, not a controlled attribution of size
changes to the refactor. The isolated builds use different absolute paths,
which can change strings retained in the image. The earlier controlled
pre-graph comparison below remains the graph-cost measurement.

A controlled comparison against the last pre-graph middleware revision
(`865d879`) used the same sample, toolchain, Zephyr tree, and build options.
The `pub` image grew from 940,096 to 1,010,472 bytes of linked flash
(+70,376 bytes), while linked DRAM stayed at 257,824 bytes. Its ELF BSS grew
by 65,536 bytes. The `sub` image grew from 1,016,792 to 1,019,372 bytes of
linked flash (+2,580 bytes), while linked DRAM stayed at 257,832 bytes and BSS
was unchanged. The retained inbound cache is allocated from the existing heap,
so its fixed 19,576-byte cost appears in the ROS allocator high-water mark,
not as extra linked DRAM.

The post-refactor inbound lifecycle measured 23,622 bytes of ROS allocator high
water and at most 125,007 bytes of middleware allocator high water across the
accepted repetitions. The bounded external heap peaked at 55,840 bytes, while
the internal libc heap peaked at 120,528 bytes. Twelve reported threads
reserved 70,144 bytes of stack in total; the tightest measured margin was 496
unused bytes in the 8 KiB `dq.builtins` stack. All allocator live-byte counters
returned to zero during cleanup.

ESP32-S3 PSRAM is used only for large plain-data allocations: RCL allocations
of at least 4 KiB (including the bounded graph arrays) and DDS allocations of
at least 8 KiB (the configured receive buffer). Smaller Cyclone control objects
remain in internal RAM because they contain synchronization and atomic state.
Access to Zephyr's shared external heap is serialized by the application.

Both outbound graph lifecycles passed against one continuously running stock
observer. The CLI snapshot showed `/ros2_zephyr_esp32s3` and both sides of
`/ros2_zephyr/graph_local`; each lifecycle then removed the endpoints and node.
The inbound lifecycle passed all five topology phases and validated three
participants, three nodes, and five remote endpoints at its largest phase.
After one default-period restart timeout was retained as a failed run, the
inbound-only SPDP interval was reduced to five seconds. The rebuilt image then
passed three consecutive participant-loss and restart lifecycles. Other roles
retain Cyclone's default interval.

The post-graph QoS regression also passed in both directions for Best
Effort/Volatile depth 5, Reliable/Volatile depth 5, and Reliable/Transient
Local depths 1 and 3. The depth-1 late joiners received `5, 6`; the depth-3
late joiners received `3, 4, 5, 6`. Reliable publishers allow two seconds for
the final DDS acknowledgment exchange because this RMW does not implement
`rmw_publisher_wait_for_all_acked()`.

Physical acceptance is complete only when both graph directions pass on the
board, the device disappears from the desktop graph after cleanup and reset,
the resource markers are captured, and the four existing QoS profiles pass
again. A cross-build alone is not physical acceptance.

Failures encountered while establishing this profile are recorded in
[the Cyclone troubleshooting notes](cyclone-troubleshooting.md).
