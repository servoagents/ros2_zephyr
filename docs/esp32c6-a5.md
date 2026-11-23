# ESP32-C6 A5 memory profile

A5 reduces RAM use for the fixed-size C6 pub/sub sample. It is opt-in; A4
remains the default.

```sh
source build/c6/env.sh
export APP_PEER_IP=192.168.1.10  # ROS desktop address
APP_PROFILE=esp32c6-a5 bash scripts/build_c6_app.sh
```

## Changes

- Share the rcutils error state instead of reserving it in every thread.
  **Only one thread may use ROS and rcutils error handling.** The sample does
  this in main, including its executor callbacks. This restriction does not
  follow automatically from using rclc.
- Reduce stack reservations by the removed TLS overhead, keeping A4's
  execution budgets, except for 512 extra bytes on idle: the first hardware
  trial left only 76 unused bytes there. TLS means thread-local storage here.
- Disable TCP: this deployment uses UDP.
- Use synchronous minimal logging, removing the deferred log thread and
  buffer. Logging now consumes the caller's stack and time.

Freed static RAM can be assigned to the application or larger heap reservations;
it is not automatically free space inside the two fixed heaps. Their configured
sizes remain 65,536 and 119,808 bytes.

## Inspect a build and a running board

```sh
APP_PROFILE=esp32c6-a5 APP_EXTRA_CONF="$PWD/samples/esp32c6_pubsub/report.conf" \
  bash scripts/build_c6_app.sh "$PWD/samples/esp32c6_pubsub" "$PWD/build/c6/a5-report"
python tools/ram_ledger.py build/c6/a5-report
```

The optional report prints per-thread unused stack bytes, heap usage and heap
peaks after Wi-Fi joins, after ROS starts and every 30 seconds. Read the return
codes as well as the values. Missing reports are not a successful test.
Check stack margins while exercising traffic, peer restarts and error logging;
unchanged stack budgets do not prove unchanged stack use.

The RAM ledger reuses the ELF totals and attributes map entries to libraries,
heaps and stacks. Merged strings, padding and unmatched sections remain an
explicit unattributed row. Reserved heaps are counted once, not added again
when runtime usage is measured.

## ESP32-C6 measurement, 2026-11-23

The flashed A5 image used **388,272 bytes of RAM**, including optional reporting
and a test-only largest-free-block probe. The rebuilt A4 sample used 431,824
bytes: A5 saved **43,552 bytes**. TLS fell from 2,576 to 532 bytes per thread.

A stock ROS 2 Kilted CycloneDDS peer connected once and then restarted ten times
while the board stayed running. Each connection exchanged values 1 through 10:
all **110 echoes** returned, with one device startup and no application failure.
The peer shut down with SIGINT between connections. The smallest observed unused
stack was **588 bytes** on idle, after its 512-byte correction.

The kernel heap peaked at 34,972 bytes and the libc heap at 102,512 bytes.
After the final disconnect, libc had 42,284 bytes free but its largest free
block was only 14,212 bytes; the smallest observed block during the run was
8,244 bytes. A5 creates useful static RAM headroom, but does not solve heap
fragmentation. This run tested peer restarts over Wi-Fi, not Wi-Fi outages or
long-duration operation.

[Recorded measurements](validation/c6-a5-2026-11-23.json) include per-thread
stack minima and image hashes. Reporting ran every five seconds in this test;
the largest-free-block probe is lab instrumentation, not part of the profile.
