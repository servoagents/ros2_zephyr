#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
role="sub"
rmw="cyclonedds_c"
reliability="best_effort"
durability="volatile"
depth="5"
environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"
credentials_file="${ROS2_ZEPHYR_WIFI_ENV_FILE:-${repository_root}/build/wifi.env}"

usage() {
  echo "usage: $0 [--rmw cyclonedds_c|zenoh_pico]" \
    "[--role node|pub|sub|pubsub] [--reliability best_effort|reliable]" \
    "[--durability volatile|transient_local] [--depth N]" >&2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rmw)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      rmw="$2"
      shift 2
      ;;
    --role)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      role="$2"
      shift 2
      ;;
    --reliability)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      reliability="$2"
      shift 2
      ;;
    --durability)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      durability="$2"
      shift 2
      ;;
    --depth)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      depth="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage
      exit 2
      ;;
  esac
done

if [[ "${rmw}" != "cyclonedds_c" && "${rmw}" != "zenoh_pico" ]]; then
  usage
  exit 2
fi
if [[ "${role}" != "node" && "${role}" != "pub" && "${role}" != "sub" &&
      "${role}" != "pubsub" ]]; then
  usage
  exit 2
fi
if [[ "${reliability}" != "best_effort" && "${reliability}" != "reliable" ]]; then
  usage
  exit 2
fi
if [[ "${durability}" != "volatile" && "${durability}" != "transient_local" ]]; then
  usage
  exit 2
fi
if [[ ! "${depth}" =~ ^[1-9][0-9]*$ ]] || ((10#${depth} > 2147483647)); then
  usage
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

profile="${rmw}-${role}-${reliability}-${durability}-depth-${depth}"
build_dir="${ROS2_ZEPHYR_WIFI_BUILD_DIR:-${repository_root}/build/${ros_distro}/wifi-esp32s3-${profile}}"
deps_root="${ROS2_ZEPHYR_DEPS_ROOT:-${repository_root}/build/deps/${ros_distro}}"
export CCACHE_DIR="${repository_root}/build/ccache"
export CCACHE_TEMPDIR="${repository_root}/build/ccache-tmp"
mkdir -p "${CCACHE_DIR}" "${CCACHE_TEMPDIR}"

"${repository_root}/verify_sources.py" \
  "${ROS2_ZEPHYR_HOST_MANIFEST}" "${deps_root}/host/src"
"${repository_root}/verify_sources.py" \
  "${ROS2_ZEPHYR_TARGET_MANIFEST}" "${deps_root}/target/src"

cmake_args=(
  -S "${sample_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DBOARD="${ROS2_ZEPHYR_BOARD_OVERRIDE:-esp32s3_devkitc/esp32s3/procpu}" \
  -DZEPHYR_MODULES="${ROS2_ZEPHYR_MODULES}" \
  -DEXTRA_CONF_FILE="${repository_root}/config/rmw/${rmw}.conf" \
  -DROS2_ZEPHYR_DEPS_ROOT="${deps_root}" \
  -DROS2_ZEPHYR_WIFI_ROLE="${role}" \
  -DROS2_ZEPHYR_WIFI_RELIABILITY="${reliability}" \
  -DROS2_ZEPHYR_WIFI_DURABILITY="${durability}" \
  -DROS2_ZEPHYR_WIFI_DEPTH="${depth}" \
  -DCMAKE_BUILD_TYPE=Release
)
if [[ "${rmw}" == "cyclonedds_c" ]]; then
  cmake_args+=(
    -DROS2_ZEPHYR_CYCLONEDDS_SOURCE="${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}"
    -DROS2_ZEPHYR_HOST_IDLC="${ROS2_ZEPHYR_HOST_IDLC}"
  )
  if [[ -n "${ROS2_ZEPHYR_RMW_SOURCE:-}" ]]; then
    cmake_args+=(
      -DROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE="${ROS2_ZEPHYR_RMW_SOURCE}"
    )
  fi
else
  zenoh_router_ipv4="${ROS2_ZEPHYR_ZENOH_ROUTER_IPV4:-${ROS_PEER_IP}}"
  cmake_args+=(
    -DROS2_ZEPHYR_ZENOH_ROUTER_IPV4="${zenoh_router_ipv4}"
    -DROS2_ZEPHYR_ZENOH_ROUTER_PORT="${ROS2_ZEPHYR_ZENOH_ROUTER_PORT:-7447}"
  )
  if [[ -n "${ROS2_ZEPHYR_RMW_SOURCE:-}" ]]; then
    cmake_args+=(
      -DROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE="${ROS2_ZEPHYR_RMW_SOURCE}"
    )
  fi
fi

ZEPHYR_BASE="${ZEPHYR_BASE}" cmake "${cmake_args[@]}"
cmake --build "${build_dir}" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-1}"

size "${build_dir}/zephyr/zephyr.elf"
printf '%s\n' "${build_dir}/zephyr/zephyr.bin"
