# Cyclone ESP32-S3 troubleshooting notes

These notes record the failures that shaped the accepted hardware profile.
They are not additional middleware requirements.

## Keep synchronization state in internal RAM

Moving every DDS allocation into PSRAM made participant cleanup fail inside
`ddsrt_mutex_lock()`. The failing caller was
`ddsi_copy_addrset_into_addrset_uc()`, and the POSIX mutex operation returned
`EINVAL`. Cyclone control objects contain mutexes, atomics, and other state
that should remain in internal RAM on this target.

The accepted allocator policy moves only large plain-data allocations:

```text
RCL allocation >= 4 KiB        -> external RAM
middleware allocation >= 8 KiB -> external RAM
smaller allocations            -> internal RAM
```

The external heap is bounded to 1 MiB and access is serialized. Do not lower
the thresholds without repeating graph churn, participant loss, Reliable, and
Transient Local hardware tests.

## Do not reuse an unverified build directory

CMake caches the Zephyr source and toolchain paths. A directory configured for
Zephyr 4.4.2 can therefore produce or flash the wrong image when a 4.4.0 run
uses `--no-build`. Use a fresh directory for release acceptance and confirm the
Zephyr version in `build.log`. The hardware matrix accepts a separate build
root for this purpose.

## Treat a discovery timeout as evidence, not a workaround request

One Reliable/Transient Local depth-3 publisher run did not discover its stock
desktop subscriber. The same 4.4.0 image passed on the immediate rerun, and the
failure did not reproduce as a depth-specific data-path problem. No retry was
added to the acceptance runner: a discovery failure remains a failed case.

The repeated inbound graph lane exposed a more specific case. After every
desktop participant had been killed, the replacement participant could miss
both of the board's default-period SPDP announcements within its 60-second
restart window. The graph `node` image now uses a five-second SPDP interval.
This is scoped to the churn test; normal data and outbound graph images retain
Cyclone's default interval. The failed run remains in the results rather than
being counted as a pass.

## Native USB can disappear during reset

The ESP32-S3 native USB Serial/JTAG device disconnects and re-enumerates during
some reset paths. The capture helper can reopen it. If USB reset reaches the
bootloader but does not start the application, run with `--no-reset` and press
RESET/EN or power-cycle while capture is armed.

## Make the desktop interface policy explicit

The deprecated `NetworkInterfaceAddress` setting left the selected interface's
multicast policy at its default in the Lyrical Cyclone build. Repeated `ros2`
CLI processes eventually hit an assertion in `set_and_check_allow_multicast()`.
The hardware runner now uses an explicit `Interfaces/NetworkInterface` entry
with its address and `multicast="false"`. This also removes the deprecated
configuration path; the firmware already has the desktop address as a static
peer.

## Participant loss is not graceful shutdown

An orderly `rclpy` shutdown publishes endpoint and graph disposal traffic. The
inbound loss test instead gives each remote graph node its own process and
kills that process, then waits for the DDS lease to expire. This exercises the
participant-loss cleanup path without conflating it with orderly teardown.

ROS 2 Lyrical can also retain a destroyed node in a graph snapshot when
several nodes share one participant. The cache preserves the received snapshot
rather than inventing a removal rule. The hardware loss test uses independent
participants so ownership and cleanup are unambiguous.
