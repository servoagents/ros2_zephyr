# Dependency patches

`rcutils-zephyr-4.4.patch` is applied while staging the ROS target workspace.
It targets the `micro-ROS/rcutils` commit pinned in
the selected distribution's target manifest.

`cyclonedds-zephyr-4.4.patch` is applied by `scripts/setup.sh` to the
official Cyclone DDS commit pinned in the selected distribution's host
manifest. Running the setup more than once does not reapply it.

`rosidl-runtime-c-fixed-profile.patch` is applied only when the selected ROS
distribution includes `rosidl_buffer`. It lets the C runtime build without a
C++ standard library by disabling native-buffer ownership while preserving the
ordinary C sequence implementation used by the fixed-size Zephyr profile.
`rosidl-typesupport-introspection-c-fixed-profile.patch` makes the matching
buffer dependency optional for introspection metadata; buffer-annotated fields
remain outside the supported profile.

These patches modify upstream projects and remain subject to their respective
upstream licenses.
