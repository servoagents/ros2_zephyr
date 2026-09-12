# Dependency patches

`rcutils-zephyr-4.4.patch` is applied while staging the ROS target workspace.
It targets the `micro-ROS/rcutils` commit pinned in
`dependencies/target.repos`.

`cyclonedds-zephyr-4.4.patch` is applied by `scripts/setup.sh` to the
official Cyclone DDS commit pinned in `dependencies/platform.repos`. Running
the setup more than once does not reapply it.

These patches modify upstream projects and remain subject to their respective
upstream licenses.
