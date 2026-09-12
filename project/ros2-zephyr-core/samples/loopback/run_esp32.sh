#!/usr/bin/env bash
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAB_ROOT="$(cd "${SAMPLE_DIR}/../../../../.." && pwd)"
SERIAL_DEVICE="${1:-/dev/ttyUSB0}"
OUTPUT_DIR="${2:-${LAB_ROOT}/results/phase5-esp32}"
LOG_FILE="${OUTPUT_DIR}/loopback.log"

mkdir -p "${OUTPUT_DIR}"
set +e
python "${SAMPLE_DIR}/capture_esp32.py" \
  "${SERIAL_DEVICE}" \
  --timeout 30 \
  --until 'PHASE5_CLEANUP status=0 ros_live=0 dds_live=0' \
  > "${LOG_FILE}" 2>&1
status=$?
set -e

if [[ "${status}" -ne 0 ]] ||
   ! rg -q 'PHASE5_LOOPBACK_PASS value=42424242 array=10,11,12,13' "${LOG_FILE}" ||
   ! rg -q 'PHASE5_ALLOC ' "${LOG_FILE}" ||
   ! rg -q 'PHASE5_STACK_TOTAL ' "${LOG_FILE}" ||
   ! rg -q 'PHASE5_CLEANUP status=0 ros_live=0 dds_live=0' "${LOG_FILE}" ||
   rg -q 'PHASE5_ERROR ' "${LOG_FILE}"; then
  cat "${LOG_FILE}" >&2
  echo "Phase 5 ESP32 loopback failed (status=${status})" >&2
  exit 1
fi

cat "${LOG_FILE}"
