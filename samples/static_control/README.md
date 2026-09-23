# Static control sample

This sample keeps ROS communication outside a 100 Hz Zephyr control thread. A
subscriber accepts a bounded target snapshot. The control thread advances a
small simulated plant. A publisher reports state at 10 Hz.

The command is `ros2_zephyr_test_msgs/msg/ControlCommand` on
`static_control/command`. Valid targets are finite values from -1.0 through
1.0. The state is `ros2_zephyr_test_msgs/msg/ControlState` on
`static_control/state`. Both endpoints use Best Effort, Volatile QoS at depth
one.

The control thread runs at priority three. The ROS-owning main thread runs at
priority four; the build rejects a main-thread priority that could preempt the
control thread.

Outputs begin disabled. A command expires 250 ms after local receipt. Expiry
sets the state to `STATUS_STALE` and disables effort until a fresh command is
accepted. A missed release or output deadline sets a latched fault. Restarting
the application is the only way to clear it.

## Build and run on native_sim

After `scripts/setup.sh`:

```sh
samples/static_control/build_native.sh
build/lyrical/static-control-native/zephyr/zephyr.exe
```

## Build and flash an ESP32-S3

Keep credentials in the shell, not in the repository:

```sh
export WIFI_SSID='your-network'
export WIFI_PSK='your-password'
export ROS_PEER_IP=192.168.0.16
samples/static_control/build_esp32.sh
west flash -d build/lyrical/static-control-esp32s3
```

The board files select USB Serial/JTAG, 32 MiB octal flash and 16 MiB PSRAM on
the tested ESP32-S3-DevKitC. The ESP32-S3 supports 2.4 GHz Wi-Fi only, so use a
2.4 GHz SSID rather than a 5 GHz-only network.

For a hardware-only forced station disconnect/reconnect probe, use a separate
build directory and the supplied extra configuration fragment:

```sh
export ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR="$PWD/build/lyrical/static-control-reconnect"
export ROS2_ZEPHYR_STATIC_CONTROL_EXTRA_CONF_FILE="$PWD/samples/static_control/prj_esp32_reconnect.conf"
samples/static_control/build_esp32.sh
west flash -d "$ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR"
```

The probe starts ten seconds after ROS initialization, holds the Wi-Fi link
down for five seconds, and reconnects. It is disabled in normal firmware.

Build `shims/ros2_zephyr_test_msgs` in the desktop ROS workspace, select domain
95, and use Best Effort when reading state:

```sh
export ROS_DOMAIN_ID=95
ros2 topic pub --once /static_control/command \
  ros2_zephyr_test_msgs/msg/ControlCommand '{target: 0.5, sequence: 1}'
ros2 topic echo --qos-reliability best_effort /static_control/state
```

## Tests

The plant test has no ROS or Zephyr dependency. The policy test runs the real
control thread and covers command bursts, expiry, invalid values and an
injected overrun:

```sh
west twister -T samples/static_control/tests/plant -p native_sim/native/64
west twister -T samples/static_control/tests/control -p native_sim/native/64
```
