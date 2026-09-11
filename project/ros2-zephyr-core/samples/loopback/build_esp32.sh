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

BUILD_DIR="${PHASE5_ESP32_BUILD_DIR:-${LAB_ROOT}/build/phase5/esp32}"
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

ZEPHYR_BASE="${ZEPHYR_BASE}" cmake \
  -S "${SAMPLE_DIR}" \
  -B "${BUILD_DIR}" \
  -G Ninja \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DBOARD="${ROS2_ZEPHYR_BOARD:-esp32_devkitc/esp32/procpu}" \
  -DCONF_FILE="${SAMPLE_DIR}/prj_esp32.conf" \
  -DZEPHYR_MODULES="${ROS2_ZEPHYR_MODULES}" \
  -DROS2_ZEPHYR_LAB_ROOT="${LAB_ROOT}" \
  -DROS2_ZEPHYR_DEPS_ROOT="${LAB_ROOT}/build/phase5/deps" \
  -DROS2_ZEPHYR_CYCLONEDDS_SOURCE="${LAB_ROOT}/externals/writable/cyclonedds" \
  -DROS2_ZEPHYR_HOST_IDLC="${HOST_IDLC}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" --parallel "${PHASE5_BUILD_JOBS:-1}"

size "${BUILD_DIR}/zephyr/zephyr.elf"
printf '%s\n' "${BUILD_DIR}/zephyr/zephyr.bin"
