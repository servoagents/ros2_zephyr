#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
role="${1:-sub}"
environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"
credentials_file="${ROS2_ZEPHYR_WIFI_ENV_FILE:-${repository_root}/build/wifi.env}"

if [[ "${role}" != "pub" && "${role}" != "sub" ]]; then
  echo "usage: $0 pub|sub" >&2
  exit 2
fi
if [[ ! -f "${environment_file}" ]]; then
  echo "missing ${environment_file}; run scripts/setup.sh" >&2
  exit 2
fi
if [[ ! -f "${credentials_file}" ]]; then
  echo "missing ${credentials_file}; define WIFI_SSID, WIFI_PSK, and ROS_PEER_IP there" >&2
  exit 2
fi

# shellcheck disable=SC1090
source "${environment_file}"
# shellcheck disable=SC1090
source "${credentials_file}"
: "${WIFI_SSID:?set WIFI_SSID in ${credentials_file}}"
: "${WIFI_PSK:?set WIFI_PSK in ${credentials_file}}"
: "${ROS_PEER_IP:?set ROS_PEER_IP in ${credentials_file}}"
export WIFI_SSID WIFI_PSK ROS_PEER_IP

build_dir="${ROS2_ZEPHYR_WIFI_BUILD_DIR:-${repository_root}/build/${ros_distro}/wifi-esp32s3-${role}}"
deps_root="${ROS2_ZEPHYR_DEPS_ROOT:-${repository_root}/build/deps/${ros_distro}}"
export CCACHE_DIR="${repository_root}/build/ccache"
export CCACHE_TEMPDIR="${repository_root}/build/ccache-tmp"
mkdir -p "${CCACHE_DIR}" "${CCACHE_TEMPDIR}"

"${repository_root}/verify_sources.py" \
  "${ROS2_ZEPHYR_HOST_MANIFEST}" "${deps_root}/host/src"
"${repository_root}/verify_sources.py" \
  "${ROS2_ZEPHYR_TARGET_MANIFEST}" "${deps_root}/target/src"

ZEPHYR_BASE="${ZEPHYR_BASE}" cmake \
  -S "${sample_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DBOARD="${ROS2_ZEPHYR_BOARD_OVERRIDE:-esp32s3_devkitc/esp32s3/procpu}" \
  -DZEPHYR_MODULES="${ROS2_ZEPHYR_MODULES}" \
  -DROS2_ZEPHYR_DEPS_ROOT="${deps_root}" \
  -DROS2_ZEPHYR_CYCLONEDDS_SOURCE="${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}" \
  -DROS2_ZEPHYR_HOST_IDLC="${ROS2_ZEPHYR_HOST_IDLC}" \
  -DROS2_ZEPHYR_WIFI_ROLE="${role}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-1}"

size "${build_dir}/zephyr/zephyr.elf"
printf '%s\n' "${build_dir}/zephyr/zephyr.bin"
