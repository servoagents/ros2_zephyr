#!/usr/bin/env bash
# Cross-build an editable application against the locked A4 library profile.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ROS2_ZEPHYR_C6_WORKSPACE:?Source build/c6/env.sh first}"
: "${ZEPHYR_SDK_INSTALL_DIR:?Source build/c6/env.sh first}"
: "${APP_PEER_IP:?Set the desktop IPv4 address}"
workspace="$ROS2_ZEPHYR_C6_WORKSPACE"
app="${1:-$root/samples/esp32c6_pubsub}"
output="${2:-$root/build/c6/application}"
app="$(realpath "$app")"
output="$(realpath -m "$output")"
credentials="${APP_NETWORK_FILE:-$root/build/c6/network.json}"
lib="$root"
cyclone="$workspace/deps/ros/platform/src/eclipse/cyclonedds"
gcc="$ZEPHYR_SDK_INSTALL_DIR/gnu/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc"
[[ "$(cat "$ZEPHYR_SDK_INSTALL_DIR/sdk_version")" == 1.0.1 ]]
[[ "$("$gcc" -dumpfullversion)" == 14.3.0 ]]
mkdir -p "$output/private"
# Input stays in a private file, outside the CMake command line and Git.
python - "$credentials" "$output/private/wifi_credentials.h" "$APP_PEER_IP" <<'PYTHON'
import ipaddress, json, os, sys
ipaddress.IPv4Address(sys.argv[3])
data = json.load(open(sys.argv[1]))
def literal(value):
    if not isinstance(value, str) or any(c in value for c in "\0\r\n"):
        raise SystemExit("Invalid Wi-Fi configuration")
    return '"' + ''.join('\\%03o' % b for b in value.encode()) + '"'
content = '#pragma once\n#define APP_WIFI_SSID ' + literal(data['ssid']) + '\n#define APP_WIFI_PSK ' + literal(data['password']) + '\n'
fd = os.open(sys.argv[2], os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
os.fchmod(fd, 0o600)
with os.fdopen(fd, 'w') as out:
    out.write(content)
PYTHON
export ZEPHYR_BASE="$workspace/deps/zephyr" ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ROS2_ZEPHYR_ROS_DISTRO=kilted
cmake -S "$cyclone" -B "$workspace/build/idlc" -G Ninja \
  -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$workspace/build/idlc-install"
cmake --build "$workspace/build/idlc" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-2}"
cmake --install "$workspace/build/idlc"
cmake -S "$app" -B "$output" -G Ninja \
  -DBOARD=esp32c6_devkitc/esp32c6/hpcore \
  -DZEPHYR_MODULES="$workspace/deps/picolibc;$workspace/deps/hal_espressif;$workspace/deps/mbedtls;$workspace/deps/tf-psa-crypto" \
  -DAPP_CREDENTIALS_DIR="$output/private" -DAPP_PEER_IP="$APP_PEER_IP" \
  -DROS2_ZEPHYR_DIR="$lib" -DROS2_ZEPHYR_DEPS_ROOT="$workspace/deps/ros" \
  -DROS2_ZEPHYR_HOST_IDLC="$workspace/build/idlc-install/bin/idlc" \
  -DROS2_ZEPHYR_CYCLONEDDS_SOURCE="$cyclone"
cmake --build "$output" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-2}"
python "$lib/tools/admit_esp32c6.py" "$output" "$output/admission.json" --objdump "${gcc%-gcc}-objdump"
printf 'Static admission recorded in %s/admission.json; hardware qualification is separate.\n' "$output"
