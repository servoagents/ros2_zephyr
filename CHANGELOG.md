# Changelog

## 2026-10-22

- Added a C6 application guide, editable UInt32 pub/sub sample, locked setup
  script and CI cross-build with static admission.
- Separated build, communication, compiler-TLS and lifecycle evidence by image.
- Let generated Cyclone deployments use the existing standard UInt32 adapter.

- Published the ESP32-C6 A4 fixed-size pub/sub profile with source pins,
  resource admission and a reproducible build path.
- Integrated the partial-magazine freelist repair in the pinned Cyclone fork.
  Upstream review is tracked separately.
- Added the benchmark evidence reference and retained the original A4 image
  hashes and qualification boundaries.

## Earlier work

- Added typed loopback, ESP32-S3 Wi-Fi interoperability, bounded graph state
  and a fixed deployment compiler.
- Added an opt-in Zephyr-native DDSRT synchronization path. Cyclone threads
  still use POSIX.
- Measured and reduced repeated wait-set allocations for fixed endpoints.
