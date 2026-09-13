#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
action="${1:-help}"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"

load_environment() {
  local environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"
  if [[ ! -f "${environment_file}" ]]; then
    echo "missing ${environment_file}; run scripts/setup.sh" >&2
    exit 2
  fi
  # shellcheck disable=SC1090
  source "${environment_file}"
}

esp32_build_dir() {
  printf '%s\n' "${ROS2_ZEPHYR_ESP32_BUILD_DIR:-${repository_root}/build/${ros_distro}/esp32}"
}

require_esp32_firmware() {
  local build_dir
  build_dir="$(esp32_build_dir)"
  if [[ ! -f "${build_dir}/zephyr/zephyr.bin" ]]; then
    echo "missing ${build_dir}/zephyr/zephyr.bin; run scripts/run.sh esp32" >&2
    exit 2
  fi
}

case "${action}" in
setup)
  exec "${repository_root}/scripts/setup.sh"
  ;;
build-native)
  exec "${repository_root}/samples/loopback/build_native.sh"
  ;;
native)
  "${repository_root}/samples/loopback/build_native.sh"
  exec "${repository_root}/samples/loopback/run_native.sh" "${2:-}"
  ;;
esp32 | build-esp32)
  exec "${repository_root}/samples/loopback/build_esp32.sh"
  ;;
flash-esp32)
  load_environment
  require_esp32_firmware
  flash_args=()
  [[ -z "${2:-}" ]] || flash_args+=(--esp-device "$2")
  exec west flash --build-dir "$(esp32_build_dir)" --runner esp32 \
    --no-rebuild "${flash_args[@]}"
  ;;
monitor-esp32)
  load_environment
  require_esp32_firmware
  monitor_args=(-e zephyr/zephyr.elf)
  [[ -z "${2:-}" ]] || monitor_args+=(-p "$2")
  cd "$(esp32_build_dir)"
  exec west espressif monitor "${monitor_args[@]}"
  ;;
run-esp32)
  "${repository_root}/samples/loopback/build_esp32.sh"
  load_environment
  require_esp32_firmware
  flash_args=()
  [[ -z "${2:-}" ]] || flash_args+=(--esp-device "$2")
  west flash --build-dir "$(esp32_build_dir)" --runner esp32 \
    --no-rebuild "${flash_args[@]}"
  exec "${repository_root}/samples/loopback/run_esp32.sh" \
    "${2:-/dev/ttyUSB0}" "${3:-}"
  ;;
run-native)
  exec "${repository_root}/samples/loopback/run_native.sh" "${2:-}"
  ;;
help | -h | --help)
  echo "usage: scripts/run.sh setup"
  echo "       scripts/run.sh native [results-directory]"
  echo "       scripts/run.sh esp32"
  echo "       scripts/run.sh run-esp32 [serial-device] [results-directory]"
  echo "       scripts/run.sh build-native | build-esp32"
  echo "       scripts/run.sh run-native [results-directory]"
  echo "       scripts/run.sh flash-esp32 | monitor-esp32 [serial-device]"
  ;;
*)
  echo "unknown action: ${action}" >&2
  exit 2
  ;;
esac
