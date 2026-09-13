#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "${SAMPLE_DIR}/../.." && pwd)"
ROS_DISTRO="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
ENVIRONMENT_FILE="${ROS2_ZEPHYR_ENV_FILE:-${REPOSITORY_ROOT}/build/zephyr-env-${ROS_DISTRO}.sh}"
SERIAL_DEVICE="${1:-/dev/ttyUSB0}"
OUTPUT_DIR="${2:-${REPOSITORY_ROOT}/results/${ROS_DISTRO}/esp32}"
LOG_FILE="${OUTPUT_DIR}/loopback.log"
PYTHON_EXECUTABLE="${PYTHON:-python3}"

if [[ ! -f "${ENVIRONMENT_FILE}" ]]; then
  echo "missing ${ENVIRONMENT_FILE}; run scripts/setup.sh" >&2
  exit 2
fi
# shellcheck disable=SC1090
source "${ENVIRONMENT_FILE}"

mkdir -p "${OUTPUT_DIR}"
set +e
"${PYTHON_EXECUTABLE}" "${SAMPLE_DIR}/capture_esp32.py" \
  "${SERIAL_DEVICE}" \
  --timeout 30 \
  --until 'ROS2_ZEPHYR_CLEANUP status=0 ros_live=0 dds_live=0' \
  >"${LOG_FILE}" 2>&1
status=$?
set -e

if [[ "${status}" -ne 0 ]] ||
  ! rg -q 'ROS2_ZEPHYR_LOOPBACK_PASS value=42424242 array=10,11,12,13' "${LOG_FILE}" ||
  ! rg -q 'ROS2_ZEPHYR_ALLOC ' "${LOG_FILE}" ||
  ! rg -q 'ROS2_ZEPHYR_STACK_TOTAL ' "${LOG_FILE}" ||
  ! rg -q 'ROS2_ZEPHYR_CLEANUP status=0 ros_live=0 dds_live=0' "${LOG_FILE}" ||
  rg -q 'ROS2_ZEPHYR_ERROR ' "${LOG_FILE}"; then
  cat "${LOG_FILE}" >&2
  echo "ROS 2 Zephyr ESP32 loopback failed (status=${status})" >&2
  exit 1
fi

cat "${LOG_FILE}"
