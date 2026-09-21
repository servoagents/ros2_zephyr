# RMW backends

`ros2_zephyr` selects one RMW implementation at build time. Applications use
the normal `rclc`, `rcl`, and RMW APIs; middleware setup and allocator hooks
belong to the selected backend.

The current backend is selected with:

```text
CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C=y
```

The Kconfig choice permits exactly one backend. Adding another backend means
adding its build description under `cmake/rmw/`, its target build script under
`rmw/`, its ROSIDL type-support selection, and any narrow platform hooks under
`platform/rmw/`. It must not add a second application API or expose
middleware-named message packages to samples.

## Capability profile

| Capability | Cyclone DDS C | Zenoh-Pico |
| --- | --- | --- |
| Publisher/subscriber | yes | not integrated |
| Best Effort | yes | to be measured |
| Reliable | yes | to be measured |
| Transient Local | yes | to be measured |
| Finite Keep Last | yes | to be measured |
| Fixed-size messages | yes | to be validated |
| ROS graph outbound | yes | to be measured |
| ROS graph inbound | bounded | to be measured |
| Services | no | out of scope |
| Actions | no | out of scope |

The Zenoh work will use the local `rmw_zenoh_pico` fork. Revision
`f44b8aff801a3bfee7a34a64961d9570e92aeed2` is the starting candidate because
it contains the Zephyr module integration. The separate
`prep/zenoh-pico-1.10.1` revision contains newer Zenoh-Pico compatibility work
but does not contain that module layer. The next milestone will establish the
ROS, Zenoh-Pico, router, and desktop `rmw_zenoh_cpp` version matrix before
choosing or combining those branches.

Unsupported entries are left explicit. The two transports need not implement
the same mechanism, but they must be described in terms of observable ROS
behavior.
