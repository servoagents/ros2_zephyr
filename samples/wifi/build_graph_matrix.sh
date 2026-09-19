#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
rmw_source="${ROS2_ZEPHYR_RMW_SOURCE:-$(realpath "${repository_root}/../rmw_cyclonedds_c")}"
output_dir="${1:-${repository_root}/results/${ros_distro}/graph-esp32s3-build}"

if [[ ! -d "${rmw_source}/rmw_cyclonedds_c" ]]; then
  echo "ROS2_ZEPHYR_RMW_SOURCE must name the rmw_cyclonedds_c repository" >&2
  exit 2
fi

mkdir -p "${output_dir}"
: >"${output_dir}/summary.log"

for role in node pub sub pubsub; do
  build_dir="${repository_root}/build/${ros_distro}/wifi-esp32s3-graph-${role}"
  log_file="${output_dir}/${role}.log"
  printf 'BUILD role=%s\n' "${role}" | tee -a "${output_dir}/summary.log"
  ROS2_ZEPHYR_RMW_SOURCE="${rmw_source}" \
    ROS2_ZEPHYR_WIFI_BUILD_DIR="${build_dir}" \
    "${sample_dir}/build_esp32.sh" --role "${role}" --reliability reliable \
    --durability volatile --depth 5 2>&1 | tee "${log_file}"
  linked_flash="$(awk '$1 == "FLASH:" {value = $2} END {print value}' "${log_file}")"
  linked_dram="$(awk '$1 == "dram0_0_seg:" {value = $2} END {print value}' "${log_file}")"
  if [[ -z "${linked_flash}" || -z "${linked_dram}" ]]; then
    echo "missing linked-memory totals for role ${role}" >&2
    exit 1
  fi
  size_line="$(size "${build_dir}/zephyr/zephyr.elf" | tail -n 1)"
  printf 'PASS role=%s linked_flash=%s linked_dram=%s size=%s\n' \
    "${role}" "${linked_flash}" "${linked_dram}" "${size_line}" | \
    tee -a "${output_dir}/summary.log"
done

echo "PASS graph ESP32-S3 build matrix" | tee -a "${output_dir}/summary.log"
