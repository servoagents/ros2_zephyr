# Project status

`ros2_zephyr` supports fixed-size ROS 2 C pub/sub deployments on the targets
listed in the README. The default Cyclone integration uses POSIX DDSRT
services. Zephyr-native synchronization is opt-in and retains POSIX threads.

## Known limits

- The tested ESP32-S3 compiler TLS implementation aliases storage across
  threads. Native DDSRT threading is unsupported on that toolchain; see
  [the hardware probe](docs/compiler-tls.md).
- The native reader/writer lock prefers waiting writers and has not been
  qualified for nested reads behind a queued writer.
- Services, actions, general variable-size messages, DDS Security and
  extended graph APIs are outside the current embedded profile.
- Static resource admission does not replace runtime stack and heap
  measurements on each board and workload.

## Planned work

1. Qualify the packaged C6 image and editable application on hardware, retaining
   image identities, failed trials, stack margins and peer-return results.
   Keep the cross-build environment reproducible before expanding the profile.
2. Upstream narrowly scoped Cyclone changes with native tests and measured
   impact. Keep each contribution independent of the embedded integration.
3. Extend fixed deployment coverage only with explicit resource budgets,
   interoperability tests and hardware evidence.

Native DDSRT threading remains exploratory. Any replacement for the current
POSIX workers needs isolated compiler TLS, explicit ownership and join behavior,
and a measured benefit. The S3 TLS investigation is separate from the C6 path.
