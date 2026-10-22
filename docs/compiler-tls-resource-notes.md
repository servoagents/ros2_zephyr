# Compiler TLS and per-thread memory

The standalone Zephyr probe checks native and POSIX workers, initialized and
zero-initialized variables, 16-byte alignment, a variable in another
translation unit, and thread reuse. Four `native_sim` cases pass. The ESP32-S3
build still links `__emutls_get_address`; a compile does not establish
per-thread isolation on hardware. Earlier hardware runs of the smaller probe
failed for both worker APIs. No ROS/Cyclone firmware was reflashed for this
expanded probe.

Zephyr's Xtensa TLS stack setup reserves linker-rounded TLS data plus two
pointer widths inside each stack. The pinned SDK/compiler uses emulated TLS,
and the tested ESP32-S3 configuration does not enable native compiler TLS.
The platform must pass the small probe before native DDSRT threads can be
qualified.

## Diagnostic storage

| Host object | Baseline | Experiment | Change |
| --- | ---: | ---: | ---: |
| Cyclone DDSRT log buffer | 2,056 B | 520 B | -1,536 B per thread |
| rcutils formatted-error cache | 2,041 B | 1,017 B | -1,024 B per thread |

These host ELF sizes do not predict linker-rounded ESP32-S3 stack
reservations. The Cyclone fork's log-buffer size option defaults to 2,048
bytes; the 512-byte experiment passed nine nonfatal logging tests at both
sizes. Shorter buffers retain less diagnostic text.

The rcutils experiment is outside pinned dependencies. A host probe passed
repeated reads, reset, overwrite and two-thread isolation. GCC `-O2
-fstack-usage` reports 32 B to 1,072 B for error-string formatting and
7,232 B to 8,272 B for the overwrite path. The added stack cost precludes
treating its TLS reduction as a demonstrated RAM saving on ESP32-S3.

A native-TLS resource check must account for TLS within each stack, along
with execution headroom and measured high-water marks. It must reject
emulated-TLS images; linked static RAM alone is insufficient. The current
ESP32-S3 image cannot supply that qualification evidence.
