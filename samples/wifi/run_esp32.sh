#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
role="sub"
serial_device=""
reliability="best_effort"
durability="volatile"
depth="5"
build_image=true
environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"

usage() {
  echo "usage: $0 [--role pub|sub] [--device PATH]" \
    "[--reliability best_effort|reliable]" \
    "[--durability volatile|transient_local] [--depth N] [--no-build]" >&2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --role)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      role="$2"
      shift 2
      ;;
    --device)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      serial_device="$2"
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
    --no-build)
      build_image=false
      shift
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

if [[ "${role}" != "pub" && "${role}" != "sub" ]]; then
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

if [[ "${build_image}" == true ]]; then
  "${sample_dir}/build_esp32.sh" --role "${role}" --reliability "${reliability}" \
    --durability "${durability}" --depth "${depth}"
fi
# shellcheck disable=SC1090
source "${environment_file}"

profile="${role}-${reliability}-${durability}-depth-${depth}"
build_dir="${ROS2_ZEPHYR_WIFI_BUILD_DIR:-${repository_root}/build/${ros_distro}/wifi-esp32s3-${profile}}"
flash_args=()
[[ -z "${serial_device}" ]] || flash_args+=(--esp-device "${serial_device}")
west flash --build-dir "${build_dir}" --runner esp32 --no-rebuild "${flash_args[@]}"
