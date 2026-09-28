# Zephyr compiler TLS qualification

Milestone C requires isolated `__thread` storage: Cyclone DDS uses it for its
DDSI thread-state pointer, logging buffer and freelist state. This standalone
Zephyr test starts two native workers, holds both live at a barrier, and checks
that their TLS addresses and values remain distinct from each other and main.
It does not build Cyclone DDS or use Wi-Fi.

After sourcing `build/zephyr-env-lyrical.sh`, build from `ros2_zephyr`:

```sh
ZEPHYR_TOOLCHAIN_VARIANT=host west build -b native_sim/native/64 \
  samples/thread_tls_qualification -d build/phase4-c/tls-native -- \
  -DZEPHYR_MODULES="$ROS2_ZEPHYR_WORKSPACE/modules/lib/picolibc"
west build -b esp32s3_devkitc/esp32s3/procpu \
  samples/thread_tls_qualification -d build/phase4-c/tls-esp32s3 -- \
  -DZEPHYR_MODULES="$ROS2_ZEPHYR_MODULES"
west flash -d build/phase4-c/tls-esp32s3 --runner esp32 --no-rebuild \
  --esp-device /dev/ttyACM0
python samples/loopback/capture_esp32.py /dev/ttyACM0 --reconnect \
  --timeout 30 --until THREAD_TLS_QUALIFICATION_
```

The test prints `THREAD_TLS_QUALIFICATION_PASS` only if the three values and
the two worker addresses are isolated. A `FAIL` is a target/toolchain
qualification failure, not an expected application success. Do not enable
native DDSRT threads on a failing target merely because a host simulator
passes.
