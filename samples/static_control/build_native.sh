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

build_dir="${ROS2_ZEPHYR_STATIC_CONTROL_BUILD_DIR:-${repository_root}/build/${ros_distro}/static-control-native}"
deps_root="${ROS2_ZEPHYR_DEPS_ROOT:-${repository_root}/build/deps/${ros_distro}}"
export CCACHE_DIR="${repository_root}/build/ccache"
export CCACHE_TEMPDIR="${repository_root}/build/ccache-tmp"
mkdir -p "${CCACHE_DIR}" "${CCACHE_TEMPDIR}"

"${repository_root}/verify_sources.py" "${ROS2_ZEPHYR_HOST_MANIFEST}" "${deps_root}/host/src"
"${repository_root}/verify_sources.py" "${ROS2_ZEPHYR_TARGET_MANIFEST}" "${deps_root}/target/src"

export ZEPHYR_TOOLCHAIN_VARIANT=host
ZEPHYR_BASE="${ZEPHYR_BASE}" cmake \
  -S "${sample_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DPython3_EXECUTABLE="$(command -v python)" \
  -DBOARD=native_sim/native/64 \
  -DZEPHYR_MODULES="${ROS2_ZEPHYR_WORKSPACE}/modules/lib/picolibc" \
  -DROS2_ZEPHYR_DEPS_ROOT="${deps_root}" \
  -DROS2_ZEPHYR_CYCLONEDDS_SOURCE="${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}" \
  -DROS2_ZEPHYR_HOST_IDLC="${ROS2_ZEPHYR_HOST_IDLC}" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}" --parallel "${ROS2_ZEPHYR_BUILD_JOBS:-1}"

echo "${build_dir}/zephyr/zephyr.exe"
