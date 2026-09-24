# Fixed deployment specialization

The deployment compiler emits a build-time feature contract alongside the
ordinary rclc setup. A fixed publisher/subscription deployment selects only
its declared message packages and sets `RCLC_ENABLE_ACTIONS=OFF`. Deployments
that do not use the compiler retain upstream rclc behavior with actions on.

The repository carries the change as an upstream-shaped rclc CMake option in
`patches/rclc-optional-actions.patch`. When disabled, rclc omits its action
sources, action headers in the public executor surface, action executor cases,
the `rcl_action` link target, and its exported dependency. The upstream default
is unchanged.

The ROS package build still visits `rcl_action` because the pinned upstream
`package.xml` declares it unconditionally. This preserves package metadata and
does not affect the final image: the final Cyclone DDS and Zenoh-Pico link maps
contain no `rcl_action`, `action_msgs`, or action UUID symbols.

## Static-control result

Measurements use the ESP32-S3 native USB console configuration, the same
Lyrical dependency set, Zephyr 4.4.0, Zephyr SDK 1.0.1, release configuration,
and Cyclone DDS backend. The release baseline is an exact detached build of
`v0.2.0-alpha.1`.

| Image | FLASH | DRAM | text | data | bss | ELF total |
|---|---:|---:|---:|---:|---:|---:|
| `v0.2.0-alpha.1` | 1,014,652 | 261,288 | 904,732 | 29,616 | 998,844 | 1,933,192 |
| compiler before action specialization | 1,019,164 | 261,976 | 909,592 | 30,316 | 998,156 | 1,938,064 |
| action-free fixed deployment | 1,015,956 | 261,976 | 900,788 | 30,316 | 998,156 | 1,929,260 |

The specialization removes 8,804 bytes of text and 3,208 bytes from Zephyr's
FLASH accounting relative to the immediately preceding compiler image. It is
also 3,944 bytes smaller in text and 3,932 bytes smaller in total ELF sections
than the exact release baseline. The newer generated deployment and data
layout leave Zephyr's FLASH-region total 1,304 bytes above the old release.

The final map contains only the declared
`ros2_zephyr_test_msgs/msg/ControlCommand` and
`ros2_zephyr_test_msgs/msg/ControlState` message type-support handles. Generated
setup initializes an executor with exactly one handle and calls
`rclc_executor_prepare()` before entering the application loop.

## Runtime acceptance

The specialized native Cyclone DDS image reached READY and ran for 20 seconds
with repeated metrics reports and zero ROS steady-state allocation attempts.
Both supported native backends built successfully from the same deployment.

On the ESP32-S3, the specialized UART-console image joined Wi-Fi, reached
READY, and reported zero ROS steady-state allocation attempts. The known
filesystem mount and Cyclone worker thread-registration diagnostics remain unchanged.

This is the first useful Milestone C reduction, so specialization stops at the
optional-action boundary. Graph and logging specialization remain separate
future experiments because they change declared semantics or diagnostic
coverage and are not needed to satisfy this milestone's exit condition.
