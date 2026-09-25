#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

: "${ROS2_ZEPHYR_CYCLONE_PREFIX:?}"
: "${ROS2_ZEPHYR_HOST_IDLC:?}"
: "${ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES:?}"
: "${ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE:?}"
: "${ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS:?}"
: "${ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES:?}"
: "${ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS:?}"

generator_source="${ROS2_ZEPHYR_MODULE_DIR}/rmw/cyclonedds_c/shims/rosidl_core_generators"
generator_link="${TARGET_SRC}/local/rosidl_core_generators"
if [[ -L "${generator_link}" ]]; then
  ln -sfn "${generator_source}" "${generator_link}"
elif [[ ! -e "${generator_link}" ]]; then
  ln -s "${generator_source}" "${generator_link}"
else
  echo "cannot stage Cyclone ROSIDL generators over ${generator_link}" >&2
  return 1
fi

if [[ -n "${ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE:-}" ]]; then
  staged_rmw_source="${TARGET_SRC}/servoagents-rmw_cyclonedds_c"
  mkdir -p "${staged_rmw_source}"
  command -v rsync >/dev/null || {
    echo "rsync is required for a local rmw_cyclonedds_c source override" >&2
    return 1
  }
  rsync --archive --delete --delete-excluded --exclude .git --exclude results \
    "${ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE}/" "${staged_rmw_source}/"
fi

staged_rmw_source="${TARGET_SRC}/servoagents-rmw_cyclonedds_c"
fixed_waitset_patch="${ROS2_ZEPHYR_MODULE_DIR}/patches/rmw-cyclonedds-c-fixed-waitset.patch"
if git -C "${staged_rmw_source}" apply --check "${fixed_waitset_patch}" >/dev/null 2>&1; then
  git -C "${staged_rmw_source}" apply "${fixed_waitset_patch}"
elif ! git -C "${staged_rmw_source}" apply --reverse --check \
  "${fixed_waitset_patch}" >/dev/null 2>&1; then
  echo "rmw_cyclonedds_c fixed-waitset patch does not apply cleanly" >&2
  return 1
fi

for ignored_package in \
  "${TARGET_SRC}/fj-blanco-rmw_zenoh_pico/rmw_zenoh_pico" \
  "${TARGET_SRC}/micro_ros-micro_ros_msgs/micro_ros_msgs" \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport_microxrcedds/rosidl_typesupport_microxrcedds_c" \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport_microxrcedds/rosidl_typesupport_microxrcedds_cpp" \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport_microxrcedds/test"; do
  [[ ! -d "${ignored_package}" ]] || touch "${ignored_package}/COLCON_IGNORE"
done

host_idlc_directory="$(dirname "${ROS2_ZEPHYR_HOST_IDLC}")"
host_idlc_library_directory="$(cd "${host_idlc_directory}/../lib" && pwd)"
export PATH="${host_idlc_directory}:${PATH}"
export LD_LIBRARY_PATH="${host_idlc_library_directory}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

colcon --log-base "${ROS2_ZEPHYR_WORK_ROOT}/log" build \
  --base-paths "${TARGET_SRC}" \
  --build-base "${TARGET_BUILD}" \
  --install-base "${TARGET_INSTALL}" \
  --merge-install \
  --executor sequential \
  --cmake-force-configure \
  --packages-up-to rclc ros2_zephyr_test_msgs rmw_cyclonedds_c \
  --packages-skip-by-dep python_cmake_module \
  --metas "${ROS2_ZEPHYR_MODULE_DIR}/colcon.meta" \
  --cmake-args \
  --no-warn-unused-cli \
  -DBUILD_SHARED_LIBS=OFF \
  -DROS2_ZEPHYR_STATIC_BUILD=ON \
  -DRCLC_ENABLE_ACTIONS="${ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS}" \
  -DROSIDL_RUNTIME_C_WITH_ROSIDL_BUFFER=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_POSITION_INDEPENDENT_CODE=OFF \
  -DCMAKE_LIBRARY_PATH="${host_idlc_library_directory}" \
  -DCMAKE_TOOLCHAIN_FILE="${ROS2_ZEPHYR_TOOLCHAIN_FILE}" \
  -DCMAKE_PREFIX_PATH="${ROS2_ZEPHYR_CYCLONE_PREFIX}" \
  -DCycloneDDS_DIR="${ROS2_ZEPHYR_CYCLONE_PREFIX}/lib/cmake/CycloneDDS" \
  -DRMW_IMPLEMENTATION=rmw_cyclonedds_c \
  -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON \
  -DRCL_LOGGING_IMPLEMENTATION=rcl_logging_noop \
  -DROSIDL_TYPESUPPORT_CYCLONEDDS_C_GENERATE_PACKAGES=ros2_zephyr_test_msgs \
  -DRMW_CYCLONEDDS_C_DEFAULT_DOMAIN_ID="${ROS2_ZEPHYR_DOMAIN_ID}" \
  -DRMW_CYCLONEDDS_C_GRAPH_MAX_NODES="${ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES}" \
  -DRMW_CYCLONEDDS_C_GRAPH_MAX_ENDPOINTS_PER_NODE="${ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE}" \
  -DRMW_CYCLONEDDS_C_GRAPH_CACHE_MAX_PARTICIPANTS="${ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS}" \
  -DRMW_CYCLONEDDS_C_GRAPH_CACHE_MAX_NODES="${ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES}" \
  -DRMW_CYCLONEDDS_C_GRAPH_CACHE_MAX_ENDPOINTS="${ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS}" \
  -DRMW_CYCLONEDDS_C_URI="${ROS2_ZEPHYR_CYCLONEDDS_URI}"
