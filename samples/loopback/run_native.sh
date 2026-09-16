#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOSITORY_ROOT="$(cd "${SAMPLE_DIR}/../.." && pwd)"
ROS_DISTRO="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
BUILD_DIR="${ROS2_ZEPHYR_NATIVE_BUILD_DIR:-${REPOSITORY_ROOT}/build/${ROS_DISTRO}/native64}"
BINARY="${BUILD_DIR}/zephyr/zephyr.exe"
OUTPUT_DIR="${1:-${REPOSITORY_ROOT}/results/${ROS_DISTRO}/native64}"
LOG_FILE="${OUTPUT_DIR}/loopback.log"

if [[ ! -x "${BINARY}" ]]; then
  echo "missing ${BINARY}; run scripts/run.sh build-native" >&2
  exit 2
fi
mkdir -p "${OUTPUT_DIR}"
set +e
timeout --signal=TERM --kill-after=3 30 "${BINARY}" --seed=205 \
  >"${LOG_FILE}" 2>&1
status=$?
set -e

if [[ "${status}" -ne 0 ]] ||
  ! grep -q '^ROS2_ZEPHYR_LOOPBACK_PASS value=42424242 array=10,11,12,13$' "${LOG_FILE}" ||
  ! grep -q '^ROS2_ZEPHYR_ALLOC ' "${LOG_FILE}" ||
  ! grep -q '^ROS2_ZEPHYR_STACK_TOTAL ' "${LOG_FILE}" ||
  ! grep -q '^ROS2_ZEPHYR_CLEANUP status=0 ros_live=0 dds_live=0$' "${LOG_FILE}"; then
  cat "${LOG_FILE}" >&2
  echo "ROS 2 Zephyr native loopback failed (status=${status})" >&2
  exit 1
fi

cat "${LOG_FILE}"
