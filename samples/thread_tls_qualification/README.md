# Zephyr compiler TLS qualification

Native DDSRT threads require isolated `__thread` storage: Cyclone DDS uses it for its
DDSI thread-state pointer, logging buffer and freelist state. This standalone
Zephyr test runs the same worker body with native `k_thread` and Zephyr
`pthread_create`. Each pair synchronizes at startup, then makes 2,000
semaphore handoffs and yields while checking TLS addresses and values against
the other worker and main. The probe checks initialized, zero-initialized,
16-byte-aligned, and cross-translation-unit TLS, and runs each API twice to
exercise thread reuse. It does not build Cyclone DDS or use Wi-Fi.

After sourcing `build/zephyr-env-lyrical.sh`, build from `ros2_zephyr`:

```sh
ZEPHYR_TOOLCHAIN_VARIANT=host west build -b native_sim/native/64 \
  samples/thread_tls_qualification -d build/phase4-c/tls-native -- \
  -DZEPHYR_MODULES="$ROS2_ZEPHYR_WORKSPACE/modules/lib/picolibc"
west build -b esp32s3_devkitc/esp32s3/procpu \
  samples/thread_tls_qualification -d build/phase4-c/tls-esp32s3-usb -- \
  -DZEPHYR_MODULES="$ROS2_ZEPHYR_MODULES" \
  -DDTC_OVERLAY_FILE="$PWD/samples/thread_tls_qualification/boards/esp32s3_devkitc_esp32s3_procpu.overlay"
west flash -d build/phase4-c/tls-esp32s3-usb --runner esp32 --no-rebuild \
  --esp-device /dev/ttyACM0
python samples/loopback/capture_esp32.py /dev/ttyACM0 --reconnect \
  --timeout 30 --until THREAD_TLS_QUALIFICATION_
```

For a compiler and Zephyr port that genuinely support native TLS, add
`-- -DEXTRA_CONF_FILE=prj_tls.conf` to the build and inspect the ELF and map
for `.tdata`/`.tbss` and absence of `__emutls_get_address`. The current
ESP32-S3 SDK emits emulated TLS and its Zephyr configuration does not select
`ARCH_HAS_THREAD_LOCAL_STORAGE`; the overlay is not an enablement switch for
that SDK. Do not flash the full ROS image as the first native-TLS test.

The test prints one `THREAD_TLS_CASE` line per API and round and a final
`THREAD_TLS_QUALIFICATION_` result. `PASS` requires distinct main/worker
addresses, stable values, zero mismatches, and no handoff timeouts. On the
pinned ESP32-S3 toolchain both APIs failed this test in four hardware runs;
`native_sim` passed both. See the
[ESP32-S3 TLS results](../../docs/compiler-tls.md). Do not
enable native DDSRT threads on a failing target merely because a host
simulator passes.
