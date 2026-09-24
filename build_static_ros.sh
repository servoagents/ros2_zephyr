#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

: "${ROS2_ZEPHYR_MODULE_DIR:?}"
: "${ROS2_ZEPHYR_DEPS_ROOT:?}"
: "${ROS2_ZEPHYR_WORK_ROOT:?}"
: "${ROS2_ZEPHYR_TOOLCHAIN_FILE:?}"
: "${ROS2_ZEPHYR_BACKEND_BUILD_SCRIPT:?}"
: "${ROS2_ZEPHYR_AR:?}"
: "${ROS2_ZEPHYR_RANLIB:?}"
: "${ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS:?}"

HOST_ROOT="${ROS2_ZEPHYR_DEPS_ROOT}/host"
TARGET_SOURCE="${ROS2_ZEPHYR_DEPS_ROOT}/target/src"
HOST_INSTALL="${HOST_ROOT}/install"
TARGET_SRC="${ROS2_ZEPHYR_WORK_ROOT}/src"
# Consumed by the selected backend build script sourced below.
# shellcheck disable=SC2034
TARGET_BUILD="${ROS2_ZEPHYR_WORK_ROOT}/build"
TARGET_INSTALL="${ROS2_ZEPHYR_WORK_ROOT}/install"
COMBINED="${ROS2_ZEPHYR_WORK_ROOT}/libros2_zephyr.a"

# local_setup.sh can exist after an interrupted partial build, so gate on the
# last host-only CMake package that the target pass needs.
host_ready="${HOST_INSTALL}/ament_cmake_ros/share/ament_cmake_ros/cmake/ament_cmake_rosConfig.cmake"
if [[ ! -f "${host_ready}" ]]; then
  CMAKE_BUILD_PARALLEL_LEVEL=1 colcon \
    --log-base "${HOST_ROOT}/log" build \
    --base-paths "${HOST_ROOT}/src" \
    --build-base "${HOST_ROOT}/build" \
    --install-base "${HOST_INSTALL}" \
    --executor sequential \
    --packages-ignore rmw_test_fixture rmw_test_fixture_implementation \
    --cmake-args -DBUILD_TESTING=OFF
fi

mkdir -p "${TARGET_SRC}/local"
for package in \
  ros2_zephyr_test_msgs \
  rosidl_core_runtime \
  rosidl_default_generators \
  rosidl_default_runtime; do
  source_path="${ROS2_ZEPHYR_MODULE_DIR}/shims/${package}"
  link_path="${TARGET_SRC}/local/${package}"
  if [[ ! -e "${link_path}" ]]; then
    ln -s "${source_path}" "${link_path}"
  fi
done

for source_group in fj-blanco micro_ros ros2 servoagents; do
  group_path="${TARGET_SOURCE}/${source_group}"
  [[ -d "${group_path}" ]] || continue
  for repository in "${group_path}"/*; do
    [[ -d "${repository}" ]] || continue
    staged_path="${TARGET_SRC}/${source_group}-$(basename "${repository}")"
    if [[ ! -e "${staged_path}" ]]; then
      cp -a "${repository}" "${staged_path}"
    fi
  done
done

rcutils_source="${TARGET_SRC}/micro_ros-rcutils"
for rcutils_patch in \
  "${ROS2_ZEPHYR_MODULE_DIR}/patches/rcutils-zephyr-4.4.patch" \
  "${ROS2_ZEPHYR_MODULE_DIR}/patches/rcutils-gcc13-atomics.patch"; do
  if git -C "${rcutils_source}" apply --check "${rcutils_patch}" >/dev/null 2>&1; then
    git -C "${rcutils_source}" apply "${rcutils_patch}"
  elif ! git -C "${rcutils_source}" apply --reverse --check \
    "${rcutils_patch}" >/dev/null 2>&1; then
    echo "rcutils compatibility patch does not apply cleanly: ${rcutils_patch}" >&2
    exit 2
  fi
done

rclc_source="${TARGET_SRC}/ros2-rclc"
rclc_patch="${ROS2_ZEPHYR_MODULE_DIR}/patches/rclc-optional-actions.patch"
if git -C "${rclc_source}" apply --check "${rclc_patch}" >/dev/null 2>&1; then
  git -C "${rclc_source}" apply "${rclc_patch}"
elif ! git -C "${rclc_source}" apply --reverse --check "${rclc_patch}" >/dev/null 2>&1; then
  echo "rclc optional-actions patch does not apply cleanly" >&2
  exit 2
fi

rosidl_runtime_source="${TARGET_SRC}/ros2-rosidl"
rosidl_runtime_patch="${ROS2_ZEPHYR_MODULE_DIR}/patches/rosidl-runtime-c-fixed-profile.patch"
if [[ -d "${rosidl_runtime_source}/rosidl_buffer" ]]; then
  if git -C "${rosidl_runtime_source}" apply --check "${rosidl_runtime_patch}" >/dev/null 2>&1; then
    git -C "${rosidl_runtime_source}" apply "${rosidl_runtime_patch}"
  elif ! git -C "${rosidl_runtime_source}" apply --reverse --check \
    "${rosidl_runtime_patch}" >/dev/null 2>&1; then
    echo "rosidl_runtime_c fixed-profile patch does not apply cleanly" >&2
    exit 2
  fi

  rosidl_introspection_patch="${ROS2_ZEPHYR_MODULE_DIR}/patches/rosidl-typesupport-introspection-c-fixed-profile.patch"
  if git -C "${rosidl_runtime_source}" apply --check \
    "${rosidl_introspection_patch}" >/dev/null 2>&1; then
    git -C "${rosidl_runtime_source}" apply "${rosidl_introspection_patch}"
  elif ! git -C "${rosidl_runtime_source}" apply --reverse --check \
    "${rosidl_introspection_patch}" >/dev/null 2>&1; then
    echo "rosidl introspection fixed-profile patch does not apply cleanly" >&2
    exit 2
  fi
fi

# Packages outside the fixed-size C runtime either require unavailable host
# facilities or add C++/dynamic-loading paths. Keep the exclusions explicit;
# some repositories remove packages between ROS distributions.
ignore_package() {
  local package_path="$1"
  [[ ! -d "${package_path}" ]] || touch "${package_path}/COLCON_IGNORE"
}

for package_path in \
  "${TARGET_SRC}/ros2-ros2_tracing/lttngpy" \
  "${TARGET_SRC}/ros2-ros2_tracing/test_tracetools" \
  "${TARGET_SRC}/ros2-rclc/rclc_examples" \
  "${TARGET_SRC}/ros2-common_interfaces/actionlib_msgs" \
  "${TARGET_SRC}/ros2-common_interfaces/std_srvs" \
  "${TARGET_SRC}/micro_ros-rcl/rcl_yaml_param_parser" \
  "${TARGET_SRC}/ros2-rcl_logging/rcl_logging_implementation" \
  "${TARGET_SRC}/ros2-rcl_logging/rcl_logging_spdlog" \
  "${TARGET_SRC}/ros2-rcl_interfaces/test_msgs" \
  "${TARGET_SRC}/ros2-rmw/rmw_security_common" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_typesupport_introspection_cpp" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_buffer" \
  "${TARGET_SRC}/ros2-rosidl_core/rosidl_core_generators" \
  "${TARGET_SRC}/ros2-rosidl_core/rosidl_core_runtime" \
  "${TARGET_SRC}/ros2-rosidl_defaults/rosidl_default_generators" \
  "${TARGET_SRC}/ros2-rosidl_defaults/rosidl_default_runtime" \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport/rosidl_typesupport_cpp" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_generator_cpp" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_runtime_cpp"; do
  ignore_package "${package_path}"
done

set +u
# The host colcon build generates this file.
# shellcheck disable=SC1091
source "${HOST_INSTALL}/local_setup.sh"
set -u
export CMAKE_BUILD_PARALLEL_LEVEL=1
export ROS2_ZEPHYR_CROSS=1

if [[ ! -f "${ROS2_ZEPHYR_BACKEND_BUILD_SCRIPT}" ]]; then
  echo "missing backend build script: ${ROS2_ZEPHYR_BACKEND_BUILD_SCRIPT}" >&2
  exit 2
fi
# The selected backend owns its package set, source override, and CMake options.
# shellcheck disable=SC1090
source "${ROS2_ZEPHYR_BACKEND_BUILD_SCRIPT}"

object_root="${ROS2_ZEPHYR_WORK_ROOT}/combined-objects"
"${CMAKE_COMMAND:-cmake}" -E remove_directory "${object_root}"
"${CMAKE_COMMAND:-cmake}" -E make_directory "${object_root}"
rm -f "${COMBINED}"
archive_index=0
while IFS= read -r archive; do
  archive_index=$((archive_index + 1))
  archive_name="$(basename "${archive}" .a)"
  extract_dir="${object_root}/${archive_index}-${archive_name}"
  mkdir -p "${extract_dir}"
  (cd "${extract_dir}" && "${ROS2_ZEPHYR_AR}" x "${archive}")
done < <(find "${TARGET_INSTALL}/lib" -type f -name '*.a' -print | sort)

mapfile -d '' objects < <(find "${object_root}" -type f -print0 | sort -z)
if [[ "${#objects[@]}" -eq 0 ]]; then
  echo "no target archives were produced" >&2
  exit 1
fi
"${ROS2_ZEPHYR_AR}" qc "${COMBINED}" "${objects[@]}"
"${ROS2_ZEPHYR_RANLIB}" "${COMBINED}"

echo "ROS2_ZEPHYR_STATIC_READY archive=${COMBINED} objects=${#objects[@]}"
