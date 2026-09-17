# ESP32-S3 Wi-Fi baseline

The accepted physical baseline exchanged `std_msgs/msg/UInt32` samples in
both directions at 1, 10, and 100 Hz. The original run used best-effort,
volatile, keep-last QoS. Reliable, volatile, keep-last has since passed on the
same ESP32-S3 in both directions against the stock Lyrical desktop RMW.

| Component | Accepted value |
| --- | --- |
| Board | ESP32-S3-DevKitC, chip revision 0.2 |
| Board memory | 32 MiB flash, 16 MiB octal PSRAM |
| `ros2_zephyr` | `v0.1.0-alpha.1`, `8750ab13fbe5a4b0a0dc95060c97b29180030073` |
| Zephyr | `v4.4.2`, `dccb09599635bdff17633fa7e9dab014b91dce90` |
| Zephyr SDK | 1.0.1, Xtensa GCC 14.3.0 |
| Cyclone DDS | 11.0.1, `2f0d07d241f62f7121749b46721049e4dea5c58b` |
| Cyclone compatibility patch | `patches/cyclonedds-zephyr-4.4.patch` at `ros2_zephyr` `8750ab1` |
| `rmw_cyclonedds_c` | `039e85860dc6032d51bd25dc552e5bdbbb0e686a` |
| Target ROS sources | Lyrical revisions in `dependencies/target-lyrical.repos` at `ros2_zephyr` `8750ab1` |
| Desktop ROS | Lyrical binary packages |
| Desktop RMW | `ros-lyrical-rmw-cyclonedds-cpp` `4.1.4-3resolute.20260811.235837` |
| Message | `std_msgs/msg/UInt32` |
| QoS | Best effort, volatile, keep last; device depth 5, desktop depth 32 |
| Network | IPv4 UDP, domain 91, multicast disabled, explicit desktop peer, Wi-Fi power save disabled |

The target ROS manifest pins, among others, `rcl`
`34bebf710402ab2c89e4cbb2aeb8b1ca8562603b` and `rclc`
`6b90796c660d22fc27df229beefdc62138f5286a`. The desktop packages in the
accepted environment included `ros-lyrical-ros-base`
`0.13.0-3resolute.20260812.070046`, Cyclone DDS
`11.0.1-4resolute.20260728.175307`, `rcl`
`10.4.4-1resolute.20260812.012321`, and `rclc`
`6.3.0-3resolute.20260812.013555`.

## Reliable acceptance

The Reliable run used domain 91 and the same 18-message schedule as the
Best Effort baseline: three messages at 1 Hz, five at 10 Hz, and ten at
100 Hz. Every sample carried the value `271828182`.

With the board as subscriber, the stock Lyrical publisher matched the device,
sent all 18 scheduled messages, sent no recovery messages, and observed the
endpoint disappear during cleanup. With the board as publisher, the stock
Lyrical subscriber received all 18 messages and reported success.

The accepted Reliable images used 1,016,916 bytes of flash and 258,088 bytes
of linked DRAM for the subscriber, and 940,148 bytes of flash and 258,080 bytes
of linked DRAM for the publisher. The native USB capture retained boot output
but not the application console after handoff. Consequently, this run verifies
wire interoperability and clean endpoint removal from the desktop's view, but
does not add Reliable allocator or stack high-water measurements. The resource
figures in the sample README remain the earlier Best Effort baseline.

The tested 32 MiB MXIC flash requires octal STR mode. DTR calibration did not
complete, and a USB reset after flashing did not reliably start the
application; the accepted runs began after a physical power cycle.

## XTypes boundary

The embedded Cyclone build sets `ENABLE_TYPELIB=OFF` and
`ENABLE_TYPE_DISCOVERY=OFF`. The stock Lyrical desktop RMW constructs dynamic
types whose member spelling differs from the generated DDS IDL (`data` versus
`data_`). Two normal TypeInformation-enabled Lyrical endpoints reject that
mismatch. The embedded endpoint does not advertise or check TypeInformation,
so the accepted hardware lane matched by DDS topic and type name.

This is the exact configuration that was tested. It does not establish that
the current C RMW interoperates with stock Lyrical when both sides enable
XTypes metadata. The Kilted Linux lane covers normal TypeInformation-enabled
interoperability.

## Preserved regression

The Wi-Fi scripts default to best effort. Run the two accepted directions with:

```sh
samples/wifi/run_esp32.sh sub /dev/ttyACM0 best_effort
python3 samples/wifi/peer.py pub --reliability best_effort
```

```sh
python3 samples/wifi/peer.py sub --reliability best_effort
samples/wifi/run_esp32.sh pub /dev/ttyACM0 best_effort
```

The public setup remains pinned to Zephyr 4.4.0. Reproducing the accepted
4.4.2 run requires an isolated `v4.4.2` west workspace; it does not require a
change to the public pin. Releases 4.4.0 through 4.4.2 use the module's
version-scoped condition-wait relock workaround.
