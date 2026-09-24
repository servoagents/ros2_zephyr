#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0

: "${ROS2_ZEPHYR_MICROCDR_PREFIX:?}"
: "${ROS2_ZEPHYR_ZENOH_PICO_PREFIX:?}"
: "${ROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE:?}"
: "${ROS2_ZEPHYR_ZENOH_ROUTER_IPV4:?}"
: "${ROS2_ZEPHYR_ZENOH_ROUTER_PORT:?}"
: "${ROS2_ZEPHYR_ZENOH_MAX_LIVELINESS_LENGTH:?}"

generator_source="${ROS2_ZEPHYR_MODULE_DIR}/rmw/zenoh_pico/shims/rosidl_core_generators"
generator_link="${TARGET_SRC}/local/rosidl_core_generators"
if [[ -L "${generator_link}" ]]; then
  ln -sfn "${generator_source}" "${generator_link}"
elif [[ ! -e "${generator_link}" ]]; then
  ln -s "${generator_source}" "${generator_link}"
else
  echo "cannot stage Zenoh ROSIDL generators over ${generator_link}" >&2
  return 1
fi

staged_rmw_source="${TARGET_SRC}/fj-blanco-rmw_zenoh_pico"
"${CMAKE_COMMAND:-cmake}" -E remove_directory "${staged_rmw_source}"
"${CMAKE_COMMAND:-cmake}" -E copy_directory \
  "${ROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE}" "${staged_rmw_source}"
if [[ ! -d "${staged_rmw_source}/rmw_zenoh_pico" ]]; then
  echo "rmw_zenoh_pico was not staged from the pinned GitHub source" >&2
  return 1
fi

rmw_patch="${ROS2_ZEPHYR_MODULE_DIR}/patches/rmw-zenoh-pico-fixed-profile.patch"
if git -C "${staged_rmw_source}" apply --check "${rmw_patch}" >/dev/null 2>&1; then
  git -C "${staged_rmw_source}" apply "${rmw_patch}"
elif ! git -C "${staged_rmw_source}" apply --reverse --check \
  "${rmw_patch}" >/dev/null 2>&1; then
  echo "rmw_zenoh_pico fixed-profile patch does not apply cleanly" >&2
  return 1
fi

for ignored_package in \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport_microxrcedds/rosidl_typesupport_microxrcedds_cpp" \
  "${TARGET_SRC}/micro_ros-rosidl_typesupport_microxrcedds/test" \
  "${TARGET_SRC}/servoagents-rmw_cyclonedds_c/rmw_cyclonedds_c" \
  "${TARGET_SRC}/servoagents-rmw_cyclonedds_c/rosidl_typesupport_cyclonedds_c"; do
  [[ ! -d "${ignored_package}" ]] || touch "${ignored_package}/COLCON_IGNORE"
done

colcon --log-base "${ROS2_ZEPHYR_WORK_ROOT}/log" build \
  --base-paths "${TARGET_SRC}" \
  --build-base "${TARGET_BUILD}" \
  --install-base "${TARGET_INSTALL}" \
  --merge-install \
  --executor sequential \
  --cmake-force-configure \
  --packages-up-to rclc ros2_zephyr_test_msgs rmw_zenoh_pico \
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
  -DCMAKE_TOOLCHAIN_FILE="${ROS2_ZEPHYR_TOOLCHAIN_FILE}" \
  -DCMAKE_PREFIX_PATH="${ROS2_ZEPHYR_MICROCDR_PREFIX};${ROS2_ZEPHYR_ZENOH_PICO_PREFIX}" \
  -Dmicrocdr_DIR="${ROS2_ZEPHYR_MICROCDR_PREFIX}/share/microcdr/cmake" \
  -Dzenohpico_DIR="${ROS2_ZEPHYR_ZENOH_PICO_PREFIX}/lib/cmake/zenohpico" \
  -DRMW_IMPLEMENTATION=rmw_zenoh_pico \
  -DRMW_IMPLEMENTATION_DISABLE_RUNTIME_SELECTION=ON \
  -DRCL_LOGGING_IMPLEMENTATION=rcl_logging_noop \
  -DWITH_ZEPHYR=ON \
  -DRMW_ZENOH_PICO_TRANSPORT_TYPE=unicast \
  -DRMW_ZENOH_PICO_CONNECT="${ROS2_ZEPHYR_ZENOH_ROUTER_IPV4}" \
  -DRMW_ZENOH_PICO_CONNECT_PORT="${ROS2_ZEPHYR_ZENOH_ROUTER_PORT}" \
  -DRMW_ZENOH_PICO_LISTEN_PORT=-1 \
  -DRMW_ZENOH_PICO_MAX_LINENESS_LEN="${ROS2_ZEPHYR_ZENOH_MAX_LIVELINESS_LENGTH}"
