# Draft for upstream discussion — Zephyr-native DDSRT synchronization

This is a local draft only; it has not been posted to Cyclone DDS upstream.

The Cyclone DDS pin `2f0d07d` builds on Zephyr by selecting POSIX DDSRT
synchronization, threads and timing. We propose an opt-in, Zephyr-only DDSRT
synchronization implementation with the same public DDSRT API. The initial
patch would leave POSIX threading and clocks selected, because the current
Zephyr thread port uses pthreads over already-static Zephyr stacks and has
separate lifecycle/cleanup concerns. Linux and default Zephyr behavior would
remain unchanged.

All four condvar families, mutexes, reader/writer locks and once must switch
together with their public type headers; a feature selector should be
generated through Cyclone's existing CMake/config convention. `k_condvar`
can back waits only after the separately pinned Zephyr 4.4.0 kernel fix for
mutex reacquisition on timeout. The native DDSRT patch should neither carry
that kernel fix nor add a second unlock/relock. Absolute DDSRT wall,
monotonic and elapsed deadlines need checked conversion to relative Zephyr
timeouts, upward rounding for positive sub-tick waits, and explicit handling
of expired, infinite and large finite values. `rwlock` must retain parallel
readers and once must publish completion to all contenders.

Proposed review split: (1) source/type selector and sync semantics/tests;
(2) separately reviewed thread slot, stack, attributes, identity, join
result and cleanup lifecycle; (3) timing adaptation where useful. Test on
native_sim and ESP32-S3 against the unchanged ROS workload, with POSIX as the
control. Do not claim a smaller image until linked symbols, stack/pool
ownership and other ROS pthread consumers are measured. Before preparing a
public PR, ask maintainers about selector naming, placement and expected
Zephyr version floor. The current draft is intentionally not an upstream
submission or a promise that the native path is already working.
