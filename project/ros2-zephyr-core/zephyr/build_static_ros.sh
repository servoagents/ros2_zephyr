#!/usr/bin/env bash
set -euo pipefail

: "${PHASE5_MODULE_DIR:?}"
: "${PHASE5_LAB_ROOT:?}"
: "${PHASE5_DEPS_ROOT:?}"
: "${PHASE5_WORK_ROOT:?}"
: "${PHASE5_TOOLCHAIN_FILE:?}"
: "${PHASE5_CYCLONE_PREFIX:?}"
: "${PHASE5_HOST_IDLC:?}"
: "${PHASE5_AR:?}"
: "${PHASE5_RANLIB:?}"

HOST_ROOT="${PHASE5_DEPS_ROOT}/host"
TARGET_SOURCE="${PHASE5_DEPS_ROOT}/target/src"
HOST_INSTALL="${HOST_ROOT}/install"
TARGET_SRC="${PHASE5_WORK_ROOT}/src"
TARGET_BUILD="${PHASE5_WORK_ROOT}/build"
TARGET_INSTALL="${PHASE5_WORK_ROOT}/install"
COMBINED="${PHASE5_WORK_ROOT}/libros2_zephyr.a"

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
  phase4_test_msgs \
  rmw_cyclonedds_c \
  rosidl_typesupport_cyclonedds_c
do
  source_path="${PHASE5_LAB_ROOT}/project/ros2-zephyr-core/packages/${package}"
  link_path="${TARGET_SRC}/local/${package}"
  if [[ -L "${link_path}" ]]; then
    current="$(readlink -f "${link_path}")"
    if [[ "${current}" != "${source_path}" ]]; then
      echo "wrong source link: ${link_path} -> ${current}" >&2
      exit 2
    fi
  elif [[ -e "${link_path}" ]]; then
    echo "refusing to replace ${link_path}" >&2
    exit 2
  else
    ln -s "${source_path}" "${link_path}"
  fi
done

for package in \
  rosidl_core_generators \
  rosidl_core_runtime \
  rosidl_default_generators \
  rosidl_default_runtime
do
  source_path="${PHASE5_MODULE_DIR}/shims/${package}"
  link_path="${TARGET_SRC}/local/${package}"
  if [[ ! -e "${link_path}" ]]; then
    ln -s "${source_path}" "${link_path}"
  fi
done

for source_group in micro_ros ros2; do
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
rcutils_patch="${PHASE5_MODULE_DIR}/patches/rcutils-zephyr-4.4.patch"
if git -C "${rcutils_source}" apply --check "${rcutils_patch}" >/dev/null 2>&1; then
  git -C "${rcutils_source}" apply "${rcutils_patch}"
elif ! git -C "${rcutils_source}" apply --reverse --check "${rcutils_patch}" >/dev/null 2>&1; then
  echo "rcutils Zephyr compatibility patch does not apply cleanly" >&2
  exit 2
fi

# Packages outside the fixed-size C runtime either require unavailable host
# facilities or add C++/dynamic-loading paths. Keep the exclusions explicit.
touch \
  "${TARGET_SRC}/ros2-ros2_tracing/lttngpy/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-ros2_tracing/test_tracetools/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rclc/rclc_examples/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-common_interfaces/actionlib_msgs/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-common_interfaces/std_srvs/COLCON_IGNORE" \
  "${TARGET_SRC}/micro_ros-rcl/rcl_yaml_param_parser/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rcl_logging/rcl_logging_spdlog/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rcl_interfaces/test_msgs/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rmw/rmw_security_common/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_typesupport_introspection_cpp/COLCON_IGNORE"

touch \
  "${TARGET_SRC}/ros2-rosidl_core/rosidl_core_generators/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rosidl_core/rosidl_core_runtime/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rosidl_defaults/rosidl_default_generators/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rosidl_defaults/rosidl_default_runtime/COLCON_IGNORE" \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport/rosidl_typesupport_cpp/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_generator_cpp/COLCON_IGNORE" \
  "${TARGET_SRC}/ros2-rosidl/rosidl_runtime_cpp/COLCON_IGNORE"

# shellcheck disable=SC1091
set +u
source "${HOST_INSTALL}/local_setup.sh"
set -u
export PATH="$(dirname "${PHASE5_HOST_IDLC}"):${PATH}"
export LD_LIBRARY_PATH="$(dirname "${PHASE5_HOST_IDLC}")/../lib${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
HOST_IDLC_LIBRARY_DIR="$(cd "$(dirname "${PHASE5_HOST_IDLC}")/../lib" && pwd)"
export CMAKE_BUILD_PARALLEL_LEVEL=1
export ROS2_ZEPHYR_CROSS=1

colcon --log-base "${PHASE5_WORK_ROOT}/log" build \
  --base-paths "${TARGET_SRC}" \
  --build-base "${TARGET_BUILD}" \
  --install-base "${TARGET_INSTALL}" \
  --merge-install \
  --executor sequential \
  --cmake-force-configure \
  --packages-up-to rclc phase4_test_msgs rmw_cyclonedds_c \
  --packages-skip-by-dep python_cmake_module \
  --metas "${PHASE5_MODULE_DIR}/colcon.meta" \
  --cmake-args \
    --no-warn-unused-cli \
    -DBUILD_SHARED_LIBS=OFF \
    -DROS2_ZEPHYR_STATIC_BUILD=ON \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=MinSizeRel \
    -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
    -DCMAKE_LIBRARY_PATH="${HOST_IDLC_LIBRARY_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${PHASE5_TOOLCHAIN_FILE}" \
    -DCMAKE_PREFIX_PATH="${PHASE5_CYCLONE_PREFIX}" \
    -DCycloneDDS_DIR="${PHASE5_CYCLONE_PREFIX}/lib/cmake/CycloneDDS" \
    -DRMW_IMPLEMENTATION=rmw_cyclonedds_c \
    -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON \
    -DROSIDL_TYPESUPPORT_CYCLONEDDS_C_GENERATE_PACKAGES=phase4_test_msgs \
    -DRMW_CYCLONEDDS_C_DEFAULT_DOMAIN_ID="${PHASE5_DOMAIN_ID}" \
    -DRMW_CYCLONEDDS_C_URI="${PHASE5_CYCLONEDDS_URI}"

object_root="${PHASE5_WORK_ROOT}/combined-objects"
"${CMAKE_COMMAND:-cmake}" -E remove_directory "${object_root}"
"${CMAKE_COMMAND:-cmake}" -E make_directory "${object_root}"
rm -f "${COMBINED}"
archive_index=0
while IFS= read -r archive; do
  archive_index=$((archive_index + 1))
  archive_name="$(basename "${archive}" .a)"
  extract_dir="${object_root}/${archive_index}-${archive_name}"
  mkdir -p "${extract_dir}"
  (cd "${extract_dir}" && "${PHASE5_AR}" x "${archive}")
done < <(find "${TARGET_INSTALL}/lib" -type f -name '*.a' -print | sort)

mapfile -d '' objects < <(find "${object_root}" -type f -print0 | sort -z)
if [[ "${#objects[@]}" -eq 0 ]]; then
  echo "no target archives were produced" >&2
  exit 1
fi
"${PHASE5_AR}" qc "${COMBINED}" "${objects[@]}"
"${PHASE5_RANLIB}" "${COMBINED}"

echo "ROS2_ZEPHYR_STATIC_READY archive=${COMBINED} objects=${#objects[@]}"
