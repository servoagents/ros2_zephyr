# ESP32-C6 A4 profile

A4 is an experimental fixed-size pub/sub profile for the ESP32-C6 HP core.
It uses the C RMW, compiler TLS, five POSIX worker threads, native DDSRT
synchronization and a 512-byte per-thread diagnostic buffer. Native
synchronization was integrated earlier; this release does not replace threads.

Include `profiles/esp32c6-a4.cmake` before `find_package(Zephyr)`. The application
still supplies its deployment, messages, networking and credentials. Use the
Kilted dependency manifests and Cyclone pin in `dependencies/platform.repos`.
Apply `scripts/apply_cyclonedds_patches.sh SOURCE` before building; the script
checks each patch and accepts an already applied patch. The logging and
partial-magazine patches are separate. Neither has been submitted upstream by
this release.

The qualified Zephyr revision is `e91e655557e622f052d091f18f9570b50e4c5a63`, with
`patches/zephyr-riscv-tls-alignment.patch`. Apply it with
`scripts/apply_zephyr_c6_tls_patch.sh SOURCE`. Do not apply the older 4.4.0
condition-variable patch to this revision. HAL/libc and toolchain pins are
recorded with the benchmark reconstruction manifest.

## Resource admission

Main reserves 11,648 bytes: 8,704 bytes of execution budget plus TLS, alignment,
architecture/sentinel allowance and a 256-byte margin. Cyclone workers reserve
11,136 bytes each. The libc arena is 119,808 bytes; the kernel heap is 65,536.
The vendor Wi-Fi provider reserves TLS inside its stack allocation. It does not
intercept generic thread creation.

```sh
python3 -m pip install -r tools/requirements.txt
python3 tools/admit_esp32c6.py BUILD report.json --objdump PATH_TO_RISCV_OBJDUMP
```

This verifier owns physical SRAM accounting and TLS-aware stack admission;
benchmark runners invoke it at their locked source revision. Heap backing and
stack backing are counted once. Runtime high-water marks are separate evidence.
The historical A4 image committed 433,488 bytes; its 16,384-byte application
reserve left 128 bytes under the 450,000-byte contract. This is contract
headroom, not free runtime heap. New builds require their own admission.

## Evidence and limits

The original image passed 20 peer returns, three fresh boots and abrupt-loss
recovery. Its frozen pilot returned 1,000/1,000 echoes. The smallest exercised
stack margin was 552 bytes; main's error path retained 600. The cache repair
reuses free objects from other partial magazines; it does not reduce the TLS
buffer or increase main's stack. Existing warning logs remain in the evidence.

These are finite exercised-path results for one Ping64 deployment, not a
production guarantee or a performance ranking. A4 has little contract headroom;
new endpoints or payloads require fresh admission. The benchmark owns radio
metadata, host orchestration, raw observations and comparison policy. The
original private source/image freezes remain separate from this formatted
release and its new builds.
