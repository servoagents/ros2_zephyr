#!/usr/bin/env bash
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAB_ROOT="$(cd "${SAMPLE_DIR}/../../../../.." && pwd)"
BUILD_DIR="${PHASE5_NATIVE_BUILD_DIR:-${LAB_ROOT}/build/phase5/native64}"
BINARY="${BUILD_DIR}/zephyr/zephyr.exe"
OUTPUT_DIR="${1:-${LAB_ROOT}/results/phase5-native64}"
LOG_FILE="${OUTPUT_DIR}/loopback.log"

if [[ ! -x "${BINARY}" ]]; then
  echo "missing ${BINARY}; run scripts/phase5.sh build-native" >&2
  exit 2
fi
mkdir -p "${OUTPUT_DIR}"
set +e
timeout --signal=TERM --kill-after=3 30 "${BINARY}" --seed=205 \
  > "${LOG_FILE}" 2>&1
status=$?
set -e

if [[ "${status}" -ne 0 ]] ||
   ! rg -q '^PHASE5_LOOPBACK_PASS value=42424242 array=10,11,12,13$' "${LOG_FILE}" ||
   ! rg -q '^PHASE5_ALLOC ' "${LOG_FILE}" ||
   ! rg -q '^PHASE5_STACK_TOTAL ' "${LOG_FILE}" ||
   ! rg -q '^PHASE5_CLEANUP status=0 ' "${LOG_FILE}"; then
  cat "${LOG_FILE}" >&2
  echo "Phase 5 native loopback failed (status=${status})" >&2
  exit 1
fi

cat "${LOG_FILE}"
