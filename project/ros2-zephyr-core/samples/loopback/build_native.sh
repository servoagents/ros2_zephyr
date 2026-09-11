#!/usr/bin/env bash
set -euo pipefail

SAMPLE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAB_ROOT="$(cd "${SAMPLE_DIR}/../../../../.." && pwd)"
ENV_FILE="${ROS2_ZEPHYR_ENV_FILE:-${LAB_ROOT}/build/zephyr-env.sh}"

if [[ ! -f "${ENV_FILE}" ]]; then
  echo "missing ${ENV_FILE}; run scripts/phase2.sh setup" >&2
  exit 2
fi
# shellcheck disable=SC1090
source "${ENV_FILE}"

BUILD_DIR="${PHASE5_NATIVE_BUILD_DIR:-${LAB_ROOT}/build/phase5/native64}"
PICOLIBC_SOURCE="${ROS2_ZEPHYR_WORKSPACE}/modules/lib/picolibc"
HOST_IDLC="${HOST_IDLC:-${LAB_ROOT}/build/phase1/cyclonedds-install/bin/idlc}"
export CCACHE_DIR="${LAB_ROOT}/build/phase5/ccache"
export CCACHE_TEMPDIR="${LAB_ROOT}/build/phase5/ccache-tmp"
mkdir -p "${CCACHE_DIR}" "${CCACHE_TEMPDIR}"

"${LAB_ROOT}/project/ros2-zephyr-core/zephyr/verify_sources.py" \
  "${LAB_ROOT}/project/ros2-zephyr-core/zephyr/dependencies/host-tools.repos" \
  "${LAB_ROOT}/build/phase5/deps/host/src"
"${LAB_ROOT}/project/ros2-zephyr-core/zephyr/verify_sources.py" \
  "${LAB_ROOT}/project/ros2-zephyr-core/zephyr/dependencies/target.repos" \
  "${LAB_ROOT}/build/phase5/deps/target/src"

export ZEPHYR_TOOLCHAIN_VARIANT=host
ZEPHYR_BASE="${ZEPHYR_BASE}" cmake \
  -S "${SAMPLE_DIR}" \
  -B "${BUILD_DIR}" \
  -G Ninja \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DBOARD=native_sim/native/64 \
  -DZEPHYR_MODULES="${PICOLIBC_SOURCE}" \
  -DROS2_ZEPHYR_LAB_ROOT="${LAB_ROOT}" \
  -DROS2_ZEPHYR_DEPS_ROOT="${LAB_ROOT}/build/phase5/deps" \
  -DROS2_ZEPHYR_CYCLONEDDS_SOURCE="${LAB_ROOT}/externals/writable/cyclonedds" \
  -DROS2_ZEPHYR_HOST_IDLC="${HOST_IDLC}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel "${PHASE5_BUILD_JOBS:-1}"

echo "${BUILD_DIR}/zephyr/zephyr.exe"
