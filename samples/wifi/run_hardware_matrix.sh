#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
serial_device=""
rmw="cyclonedds_c"
output_dir=""
build_root=""
run_graph=true
run_qos=true
build_images=true
reset_device=true
peer_container=""
peer_address=""

usage() {
  cat >&2 <<EOF
usage: $0 --device PATH [options]

Options:
  --rmw cyclonedds_c|zenoh_pico
  --output-dir PATH
  --build-root PATH
  --graph-only
  --qos-only
  --peer-container IMAGE
  --peer-address IPV4
  --no-build
  --no-reset

Cyclone runs both graph directions and both device directions for Best
Effort/Volatile, Reliable/Volatile, and Reliable/Transient Local depths 1 and
3. Zenoh-Pico runs both device directions for the two Volatile profiles;
graph discovery and Transient Local are recorded as skips. The board is
flashed and reset for every case.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --rmw)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      rmw="$2"
      shift 2
      ;;
    --device)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      serial_device="$2"
      shift 2
      ;;
    --output-dir)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      output_dir="$2"
      shift 2
      ;;
    --build-root)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      build_root="$2"
      shift 2
      ;;
    --graph-only)
      run_qos=false
      shift
      ;;
    --qos-only)
      run_graph=false
      shift
      ;;
    --peer-container)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      peer_container="$2"
      shift 2
      ;;
    --peer-address)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      peer_address="$2"
      shift 2
      ;;
    --no-build)
      build_images=false
      shift
      ;;
    --no-reset)
      reset_device=false
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

if [[ "${rmw}" != "cyclonedds_c" && "${rmw}" != "zenoh_pico" ]]; then
  usage
  exit 2
fi
if [[ -z "${serial_device}" ]]; then
  usage
  exit 2
fi

output_dir="${output_dir:-${repository_root}/results/${ros_distro}/esp32s3-hardware-${rmw}}"
mkdir -p "${output_dir}"
if [[ -n "${build_root}" ]]; then
  mkdir -p "${build_root}"
fi
matrix_summary="${output_dir}/summary.log"
: >"${matrix_summary}"

common_args=(--rmw "${rmw}" --device "${serial_device}")
if [[ "${build_images}" == false ]]; then
  common_args+=(--no-build)
fi
if [[ "${reset_device}" == false ]]; then
  common_args+=(--no-reset)
fi
if [[ -n "${peer_container}" ]]; then
  common_args+=(--peer-container "${peer_container}")
fi
if [[ -n "${peer_address}" ]]; then
  common_args+=(--peer-address "${peer_address}")
fi

run_case() {
  local role="$1"
  local reliability="$2"
  local durability="$3"
  local depth="$4"
  local case_name="${role}-${reliability}-${durability}-depth-${depth}"
  local case_dir="${output_dir}/${case_name}"
  local case_status=0

  echo "RUN ${case_name}" | tee -a "${matrix_summary}"
  if [[ -n "${build_root}" ]]; then
    ROS2_ZEPHYR_WIFI_BUILD_DIR="${build_root}/${case_name}" \
      "${sample_dir}/run_hardware_case.sh" "${common_args[@]}" \
        --role "${role}" --reliability "${reliability}" \
        --durability "${durability}" --depth "${depth}" \
        --output-dir "${case_dir}" || case_status=$?
  else
    "${sample_dir}/run_hardware_case.sh" "${common_args[@]}" \
      --role "${role}" --reliability "${reliability}" \
      --durability "${durability}" --depth "${depth}" \
      --output-dir "${case_dir}" || case_status=$?
  fi
  if [[ "${case_status}" -eq 0 ]]; then
    echo "PASS ${case_name}" | tee -a "${matrix_summary}"
  else
    echo "FAIL ${case_name}" | tee -a "${matrix_summary}" >&2
    return 1
  fi
}

if [[ "${run_graph}" == true ]]; then
  if [[ "${rmw}" == "cyclonedds_c" ]]; then
    run_case pubsub reliable volatile 5
    run_case node reliable volatile 5
  else
    echo "SKIP ROS graph: rmw_zenoh_pico has no interoperable graph discovery" |
      tee -a "${matrix_summary}"
  fi
fi

if [[ "${run_qos}" == true ]]; then
  profiles=(
    'best_effort volatile 5'
    'reliable volatile 5'
  )
  if [[ "${rmw}" == "cyclonedds_c" ]]; then
    profiles+=(
      'reliable transient_local 1'
      'reliable transient_local 3'
    )
  else
    echo "SKIP Transient Local: rmw_zenoh_pico does not retain history" |
      tee -a "${matrix_summary}"
  fi
  for profile in "${profiles[@]}"; do
    read -r reliability durability depth <<<"${profile}"
    run_case pub "${reliability}" "${durability}" "${depth}"
    run_case sub "${reliability}" "${durability}" "${depth}"
  done
fi

echo "PASS ESP32-S3 ${rmw} hardware matrix" | tee -a "${matrix_summary}"
