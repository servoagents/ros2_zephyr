# Start an ESP32-C6 ROS 2 C application

This experimental path keeps `rclc` and `rcl` and communicates directly through
Cyclone DDS. It uses the A4 resource configuration, five POSIX Cyclone workers
and native DDSRT synchronization. It does not use an XRCE Agent. Compiler TLS
means thread-local storage, not encrypted transport; DDS Security is unsupported.

## 1. Prepare the development environment

Use Linux x86-64, Git, Python **3.14** with venv, CMake >= 3.20, Ninja, a C/C++
compiler, make, rsync, dtc, xz and ordinary compiler/build headers. The setup
requires network access and space for Zephyr, ROS sources and the SDK.
The same preparation script is used by the C6 CI cross-build job.

```sh
git clone https://github.com/servoagents/ros2_zephyr.git
cd ros2_zephyr
bash scripts/setup_c6.sh
source build/c6/env.sh
```

The script prepares an isolated venv with locked packages and Zephyr SDK 1.0.1
(RISC-V GCC 14.3.0). An existing complete SDK may be supplied through
`ZEPHYR_SDK_INSTALL_DIR`. The compiler-only archive is insufficient.
The development environment reuses the exact reconstruction manifest at
`servoagents/ros2-embedded-benchmarks` commit
`8f04542` in an ignored workspace. That checkout supplies dependency locks,
patch verification and downloads; you do not edit its firmware, run its benchmark
or install benchmark message types on your ROS desktop to use this application.
The locked static-library build still includes its internal test-message package;
this example uses only `std_msgs` on the wire.

Preparation verifies source revisions, the reference release tree, patches and
Espressif blobs. The application build uses this checkout of `ros2_zephyr` with
those locked dependencies. Record this checkout's revision alongside the resulting
image. The sample and benchmark share a dependency baseline, not hardware
qualification. Setup also accepts the existing
`--source-cache FILE --blobs-cache DIRECTORY` options for verified local caches.

## 2. Copy and edit the application

```sh
cp -a samples/esp32c6_pubsub build/c6/my_application
cp samples/esp32c6_pubsub/network.example.json build/c6/network.json
chmod 600 build/c6/network.json
```

Edit the SSID and password in `build/c6/network.json`. Never supply credentials
as command-line arguments. The ignored build directory and generated firmware
can contain credentials and must remain private.

The example receives `/c6/command` and returns `/c6/state`, both
`std_msgs/msg/UInt32`. Change the callback in `src/main.c` for your application;
change `deployment.json` when altering the endpoints or types. It remains a
fixed deployment: one peer, two remote endpoints, best-effort/volatile depth 1,
domain 73 and no graph service. See [deployment generation](deployment-compiler.md)
for custom fixed-size message packages and rejection rules. `UInt32` uses the
existing C RMW adapter; it does not require generating all of `std_msgs`. Other
message packages need source and generated type-support integration; changing a
type string alone is insufficient.

## 3. Build and inspect the resource report

```sh
export APP_PEER_IP=192.168.1.10  # Replace with your ROS desktop's Wi-Fi IPv4.
bash scripts/build_c6_app.sh "$PWD/build/c6/my_application"
```

The default output is `build/c6/application`. An optional second argument selects
a different build directory; use a fresh directory for a different application.
`APP_NETWORK_FILE` can select another private JSON file.
The script runs deployment checks, cross-compilation and TLS-aware static
admission. Inspect `build/c6/application/admission.json` and the generated
resource reports before flashing. A rejected image is not ready for testing;
do not bypass admission by reducing declared margins without evidence.

These results answer whether the linked image fits its declared contract and
stack-accounting rules. They do not measure stack high-water marks, prove
worst-case execution behavior or establish peer recovery. Original A4 committed
433,488 bytes plus a 16,384-byte application reserve, leaving 128 bytes under
its 450,000-byte contract. That figure is not free heap, and the editable
application gets its own report rather than inheriting it.

## 4. Flash and run a normal ROS peer

With the intended ESP32-C6 attached, identify its serial port first:

```sh
export ZEPHYR_BASE="$ROS2_ZEPHYR_C6_WORKSPACE/deps/zephyr"
west flash -d build/c6/application --runner esp32 --esp-device /dev/ttyUSB0
python -m serial.tools.miniterm /dev/ttyUSB0 115200
```

The console reports `APP_WIFI_READY ip=...` and `C6_APP_READY`. Exit the monitor
before a tool that needs exclusive serial access. A flash writes the board;
it is intentionally a separate step from setup and build.

On a Linux ROS 2 Kilted desktop with `rclcpp`, `std_msgs` and
`rmw_cyclonedds_cpp`, copy `samples/esp32c6_pubsub/host-cyclone.xml` to a local
file and replace `YOUR_WIFI_INTERFACE` and `YOUR_MCU_IPV4`. The firmware's
`APP_PEER_IP` must identify this same desktop. Both machines need working UDP
connectivity on the Wi-Fi network.

```sh
source /opt/ros/kilted/setup.bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
export ROS_DOMAIN_ID=73
export CYCLONEDDS_URI="file://$PWD/host-cyclone.xml"
colcon --log-base build/c6/host-log build --base-paths samples/esp32c6_pubsub/host \
  --build-base build/c6/host-build --install-base build/c6/host-install
source build/c6/host-install/setup.bash
ros2 run c6_example_peer c6_example_peer
```

The small stock `rclcpp` peer disables parameter services, parameter-event
publication, rosout and the type-description service to avoid unintended
application endpoints. The peer retries values 1 through 10 and reports success only after all ten
are returned. It is a functional check, not a throughput or latency benchmark.
Do not run additional ROS peers or the ROS CLI daemon in domain 73 during this
fixed-profile test: they may exceed its declared remote capacity. Stop and
restart the peer as an additional exploratory check, retaining any failure.

## Recorded application check

On 22 October 2026, a copied sample built from a fresh dependency workspace and
passed static admission: 431,824 bytes committed RAM plus the 16,384-byte reserve,
leaving 1,792 bytes under the contract. The SDK was supplied locally; source and
blob caches were verified. A dummy-credential build also passed.

The hardware image
`62e90a22d404982c17a1066f75eb87bdcf5cbad1ecfd705d00ac68dc528fe796`
returned all ten values in two consecutive runs of the supplied ROS peer.
[The check record](validation/c6-application-2026-10-22.json) includes admission,
source hashes, tools and peer output. The ROM image-digest warning remains
unresolved despite successful programmed-byte verification and execution.
Laptop power saving was on. This is a functional smoke test; runtime stack
margins, cache stability and the full A4 lifecycle procedure were not measured.

## Evidence and qualification boundaries

- [A4 configuration and limits](esp32c6-a4.md): original Ping64 hardware results.
- [Packaged image build check](https://github.com/servoagents/ros2-embedded-benchmarks/blob/main/docs/release-check.md): clean-checkout build/static admission, originally without hardware qualification.
- [Exact-image lifecycle procedure](https://github.com/servoagents/ros2-embedded-benchmarks/blob/main/docs/lifecycle.md): separate peer-return, restart, error-path and heap/stack checks for A4 Ping64.
- [S3 TLS findings](compiler-tls.md): earlier S3 communication is not C6-equivalent qualification.

Changing callbacks, payloads, credentials, configuration or dependencies produces
an image that needs its own identity and relevant tests. This editable sample
has no inherited lifecycle qualification. When reporting a problem, include
source revisions, board, tool versions, sanitized configuration, admission
report and the first failing log. Exclude passwords and firmware containing them.
