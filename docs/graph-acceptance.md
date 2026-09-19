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

The stock peer presents two namespaced nodes, two publishers and two
subscriptions on one topic, and a publisher on a second topic. It then reduces
the topology, shuts down the participant, restarts the same node name on a new
participant with one subscription, and shuts down again.

The board queries the cache through `rcl_get_node_names()`,
`rcl_get_topic_names_and_types()`, `rcl_count_publishers()`, and
`rcl_count_subscribers()`. Each accepted phase prints
`ROS2_ZEPHYR_GRAPH_PASS`; all five phases must pass before the firmware reports
successful cleanup. The peer uses ordinary ROS graph announcements and DDS
discovery only.

## Resource evidence

Every firmware role reports linked flash/DRAM at build time. At runtime it
prints Zephyr heap peaks, ROS and DDS allocator high-water marks, thread count,
reserved stack, and per-thread unused stack. Record the `node` figures after
the complete inbound lifecycle because that run covers cache growth and
Cyclone's graph-sample deserialization.

The Lyrical/Zephyr 4.4.0 cross-build on 2026-09-19 produced:

| Role | Linked flash | Linked DRAM |
| --- | ---: | ---: |
| `node` | 1,007,312 B | 257,304 B |
| `pub` | 1,010,472 B | 257,824 B |
| `sub` | 1,019,372 B | 257,832 B |
| `pubsub` | 1,011,148 B | 257,824 B |

A controlled comparison against the last pre-graph middleware revision
(`865d879`) used the same sample, toolchain, Zephyr tree, and build options.
The `pub` image grew from 940,096 to 1,010,472 bytes of linked flash
(+70,376 bytes), while linked DRAM stayed at 257,824 bytes. Its ELF BSS grew
by 65,536 bytes. The `sub` image grew from 1,016,792 to 1,019,372 bytes of
linked flash (+2,580 bytes), while linked DRAM stayed at 257,832 bytes and BSS
was unchanged. The retained inbound cache is allocated from the existing heap,
so its fixed 19,576-byte cost appears in the ROS allocator high-water mark,
not as extra linked DRAM.

The build figures are complete. Runtime allocator, heap, thread, and stack
figures still require the application-console output from a physical board;
they are intentionally not inferred from the ELF.

Physical acceptance is complete only when both graph directions pass on the
board, the device disappears from the desktop graph after cleanup and reset,
the resource markers are captured, and the four existing QoS profiles pass
again. A cross-build alone is not physical acceptance.
