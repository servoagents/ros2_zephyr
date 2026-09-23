#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

sample_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${sample_dir}/../.." && pwd)"
ros_distro="${ROS2_ZEPHYR_ROS_DISTRO:-lyrical}"
environment_file="${ROS2_ZEPHYR_ENV_FILE:-${repository_root}/build/zephyr-env-${ros_distro}.sh}"

if [[ ! -f "${environment_file}" ]]; then
  echo "missing ${environment_file}; run scripts/setup.sh" >&2
  exit 2
fi
# shellcheck disable=SC1090
source "${environment_file}"

build_dir="${ROS2_ZEPHYR_ESP32_BUILD_DIR:-${repository_root}/build/${ros_distro}/esp32}"
deps_root="${ROS2_ZEPHYR_DEPS_ROOT:-${repository_root}/build/deps/${ros_distro}}"
deployment_backend="${ROS2_ZEPHYR_DEPLOYMENT_BACKEND:-rmw_cyclonedds_c}"
export CCACHE_DIR="${repository_root}/build/ccache"
export CCACHE_TEMPDIR="${repository_root}/build/ccache-tmp"
mkdir -p "${CCACHE_DIR}" "${CCACHE_TEMPDIR}"

backend_args=()
if [[ "${deployment_backend}" == "rmw_cyclonedds_c" ]]; then
  backend_args+=(
    "-DROS2_ZEPHYR_CYCLONEDDS_SOURCE=${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}"
    "-DROS2_ZEPHYR_HOST_IDLC=${ROS2_ZEPHYR_HOST_IDLC}"
  )
fi

"${repository_root}/verify_sources.py" \
  "${ROS2_ZEPHYR_HOST_MANIFEST}" "${deps_root}/host/src"
"${repository_root}/verify_sources.py" \
  "${ROS2_ZEPHYR_TARGET_MANIFEST}" "${deps_root}/target/src"

ZEPHYR_BASE="${ZEPHYR_BASE}" cmake \
  -S "${sample_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DBOARD="${ROS2_ZEPHYR_BOARD_OVERRIDE:-${ROS2_ZEPHYR_BOARD:-esp32_devkitc/esp32/procpu}}" \
  -DCONF_FILE="${sample_dir}/prj_esp32.conf" \
  -DZEPHYR_MODULES="${ROS2_ZEPHYR_MODULES}" \
  -DROS2_ZEPHYR_DEPS_ROOT="${deps_root}" \
  -DROS2_ZEPHYR_DEPLOYMENT_BACKEND="${deployment_backend}" \
  "${backend_args[@]}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-1}"

size "${build_dir}/zephyr/zephyr.elf"
printf '%s\n' "${build_dir}/zephyr/zephyr.bin"
