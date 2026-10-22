# Changelog

## 2026-10-22

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
