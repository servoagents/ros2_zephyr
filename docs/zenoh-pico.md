# Zenoh-Pico backend

The Zenoh path uses the same `rclc` application, allocator instrumentation,
ESP32-S3 board files, Wi-Fi setup, and message types as the Cyclone path. The
backend is selected at build time; the application does not call a Zenoh API.

## Version matrix

| Component | Revision |
| --- | --- |
| ROS 2 target | Lyrical |
| Zephyr | 4.4.0 |
| `rmw_zenoh_pico` | `f44b8aff801a3bfee7a34a64961d9570e92aeed2` |
| Zenoh-Pico | `e1ab223a28aaebb5dec1e70d98eab152332f777a` (1.10.1) |
| Micro-CDR | `ed4fd513a24a53b93d548d342cb7aa0a18716f04` |
| desktop `rmw_zenoh_cpp` | `aae224e449f8f364f4a8025fe85899ce06f5381b` (Jazzy) |
| desktop Zenoh stack | vendored by that `rmw_zenoh` revision (1.4.0) |
| desktop base image | `ros@sha256:c3706ef0a0aa45413c07803cf433602f543b22e45b4855f6fca955c2d8ecc4e8` |

Lyrical is the firmware target. Jazzy is the temporary stock desktop lane
because it is the compatibility revision named by the fork. Moving the peer to
Lyrical requires a tested `rmw_zenoh_cpp` revision; it is not inferred from
protocol compatibility alone.

All firmware dependencies are exact GitHub pins in the repository manifests.
The peer image also clones and verifies its exact GitHub commit while building.

## Build boundary

Micro-CDR and Zenoh-Pico are built as separate static libraries with the
Zephyr toolchain. The ordinary ROS target workspace builds `rmw_zenoh_pico`
and Micro XRCE-DDS C typesupport, then combines the ROS archives as it does for
the Cyclone backend. Micro XRCE-DDS transport is not linked or used.

The integration carries two narrow staged-source patches:

- `rmw-zenoh-pico-fixed-profile.patch` updates removed Lyrical build/API
  details, selects C-only typesupport, removes forced debug optimization, and
  fixes bounded event-state clearing, QoS reporting, and init/session cleanup;
- `zenoh-pico-zephyr-4.4.cmake` routes Zenoh allocations through the common
  metrics allocator and adapts the pinned Zephyr task implementation.

The pinned GitHub checkouts remain unchanged.

## Validation status

The Lyrical `native_sim/native/64` loopback application passes. On the
physical ESP32-S3, Best Effort/Volatile and Reliable/Volatile pass in both
directions against the pinned stock Jazzy peer. Every lane transferred the
complete 18-sample schedule and returned the tracked ROS and middleware live
allocation counts to zero. The Reliable desktop publisher used no recovery
samples.

The accepted Reliable subscriber uses 745,876 bytes of linked flash and
263,704 bytes of linked DRAM. The publisher uses 668,948 bytes of linked flash
and 263,696 bytes of linked DRAM. Both roles ran nine threads with 43,520
reserved stack bytes. The external allocation heap remained unused; tracked
middleware allocation peaked at 12,860 bytes for the subscriber and 13,088
bytes for the publisher.

[The full resource table](rmw-resource-comparison.md) compares all four
accepted Zenoh profiles with the matching Cyclone DDS C runs.

The fork's publisher/subscriber matched-count APIs return
`RMW_RET_UNSUPPORTED`. The sample therefore uses a bounded five-second grace
before an unmatched publisher starts sending. This is required for Best
Effort; Reliable delivery happened to mask the missing discovery wait in the
first diagnostic run.

ROS graph acceptance is skipped in both directions. The local graph query and
endpoint-count APIs are unsupported, and a physical outbound test confirmed
that the fork's liveliness metadata does not produce a stock
`rmw_zenoh_cpp` ROS graph. Transient Local is also skipped because the
publisher does not retain history. Reliable Keep Last uses Zenoh's drop
congestion policy, so the accepted exchange does not establish every ROS
Reliable semantic.

## Commands

Build the pinned stock peer once:

```sh
samples/wifi/build_zenoh_peer.sh
```

Build either application direction:

```sh
samples/wifi/build_esp32.sh --rmw zenoh_pico --role sub
samples/wifi/build_esp32.sh --rmw zenoh_pico --role pub
```

The router address is taken from `ROS_PEER_IP` in the ignored `build/wifi.env`.
The hardware runner can override it with `--peer-address`. Its managed router
uses port 7447.

With the board attached, run the supported matrix against the pinned peer:

```sh
samples/wifi/run_hardware_matrix.sh \
  --rmw zenoh_pico \
  --device /dev/ttyACM0 \
  --peer-container ros2-zephyr-zenoh-peer:jazzy-0.2.5
```

The matrix runs both device directions for Best Effort/Volatile and
Reliable/Volatile. It records explicit skips for ROS graph discovery and
Transient Local.
