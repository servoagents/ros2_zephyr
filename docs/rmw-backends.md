# RMW backends

`ros2_zephyr` selects one RMW implementation at build time. Applications use
the normal `rclc`, `rcl`, and RMW APIs; middleware setup and allocator hooks
belong to the selected backend.

Select a backend with one of these Kconfig fragments:

```text
CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C=y
CONFIG_ROS2_ZEPHYR_RMW_ZENOH_PICO=y
```

The Kconfig choice permits exactly one backend. Adding another backend means
adding its build description under `cmake/rmw/`, its target build script under
`rmw/`, its ROSIDL type-support selection, and any narrow platform hooks under
`platform/rmw/`. It must not add a second application API or expose
middleware-named message packages to samples.

## Capability profile

| Capability | Cyclone DDS C | Zenoh-Pico |
| --- | --- | --- |
| Publisher/subscriber | yes | yes, physical ESP32-S3 |
| Best Effort | yes | yes, Volatile |
| Reliable | yes | 18-sample Volatile exchange passes without recovery |
| Transient Local | yes | no retained-history implementation |
| Finite Keep Last | yes | receive queue is bounded by the requested depth |
| Fixed-size messages | yes | compile and link validated |
| ROS graph outbound | yes | not interoperable with stock `rmw_zenoh_cpp` |
| ROS graph inbound | bounded | not implemented by `rmw_zenoh_pico` |
| Services | no | out of scope |
| Actions | no | out of scope |

The Zenoh backend is fetched from the GitHub fork at revision
`f44b8aff801a3bfee7a34a64961d9570e92aeed2`; sibling source trees are not used
unless `ROS2_ZEPHYR_RMW_SOURCE` is set explicitly. Its pinned dependency and
desktop-peer matrix is recorded in [the Zenoh-Pico integration note](zenoh-pico.md).
Measured ESP32-S3 resource use is compared in
[RMW resource comparison](rmw-resource-comparison.md).

Unsupported entries are left explicit. The two transports need not implement
the same mechanism, but they must be described in terms of observable ROS
behavior.
