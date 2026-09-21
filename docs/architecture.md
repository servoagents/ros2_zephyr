# Architecture and source provenance

`ros2_zephyr` preserves the standard ROS 2 C application and RMW boundaries.
The application uses `rclc` and `rcl`; exactly one RMW backend is selected by
Kconfig and linked statically. Zephyr provides sockets, threads, clocks,
allocation, libc, and network drivers.

Common build logic prepares the ROS C stack, toolchain, allocator tracking,
and application integration. Backend directories own their middleware source,
ROSIDL type support, package set, compile definitions, compatibility patches,
and narrow platform hooks. The common loopback interface package has no
middleware dependency. The application does not initialize a middleware or
call DDS or Zenoh APIs.

Static linking controls code size; it is not the portability layer. The
Cyclone backend uses its Zephyr `ddsrt` implementation. The Zenoh backend uses
Zenoh-Pico's Zephyr platform and Micro-CDR serialization. Both use the same
selected ROS packages and the Zephyr target toolchain and ABI in the
ament/colcon sub-build.

The target workspace contains source from the following upstream projects:

- ROS 2: `rclc`, RMW interfaces and selection, ROSIDL generators and runtime,
  logging interfaces, tracing stubs, and message definitions;
- micro-ROS forks: `rcl`, `rcutils`, and the C typesupport dispatcher;
- the selected middleware backend: either Eclipse Cyclone DDS with
  `rmw_cyclonedds_c` and Cyclone C typesupport, or Zenoh-Pico with
  `rmw_zenoh_pico`, Micro-CDR, and Micro XRCE-DDS C typesupport.

Using micro-ROS-maintained source forks does not add XRCE transport or an
Agent. The firmware does not include `rmw_microxrcedds` or the Micro XRCE-DDS
client.

Host-only ament, colcon, Python ROSIDL generators, and Cyclone `idlc` produce C
sources and build metadata. They are not linked into the firmware.

All dependency manifests use commit hashes. The carried `rcutils` and Cyclone
DDS patches are temporary compatibility measures for Zephyr 4.4 and should be
removed when the corresponding fixes land upstream. Backend capabilities and
the extension boundary are listed in [RMW backends](rmw-backends.md).
