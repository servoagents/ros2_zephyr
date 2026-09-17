#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
role="${1:-sub}"
serial_device="${2:-}"
reliability="${3:-best_effort}"
environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"

if [[ "${role}" != "pub" && "${role}" != "sub" ]]; then
  echo "usage: $0 pub|sub [serial-device] [best_effort|reliable]" >&2
  exit 2
fi
if [[ "${reliability}" != "best_effort" && "${reliability}" != "reliable" ]]; then
  echo "usage: $0 pub|sub [serial-device] [best_effort|reliable]" >&2
  exit 2
fi

"${sample_dir}/build_esp32.sh" "${role}" "${reliability}"
# shellcheck disable=SC1090
source "${environment_file}"

build_dir="${ROS2_ZEPHYR_WIFI_BUILD_DIR:-${repository_root}/build/${ros_distro}/wifi-esp32s3-${role}-${reliability}}"
flash_args=()
[[ -z "${serial_device}" ]] || flash_args+=(--esp-device "${serial_device}")
west flash --build-dir "${build_dir}" --runner esp32 --no-rebuild "${flash_args[@]}"
