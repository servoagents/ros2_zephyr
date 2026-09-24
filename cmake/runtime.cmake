# SPDX-License-Identifier: Apache-2.0

set(ROS2_ZEPHYR_C_COMPILER "${CMAKE_C_COMPILER}")
set(ROS2_ZEPHYR_CXX_COMPILER "${CMAKE_CXX_COMPILER}")
set(ROS2_ZEPHYR_AR "${CMAKE_AR}")
set(ROS2_ZEPHYR_RANLIB "${CMAKE_RANLIB}")

if(ROS2_ZEPHYR_DEPLOYMENT_PLAN)
  set(ros2_zephyr_resource_tool
      "${CMAKE_CURRENT_LIST_DIR}/../tools/verify_resources.py"
  )
  set(ros2_zephyr_resource_plan
      "${ROS2_ZEPHYR_DEPLOYMENT_OUTPUT_DIR}/resource-plan.json"
  )
  set(ros2_zephyr_resource_plan_markdown
      "${ROS2_ZEPHYR_DEPLOYMENT_OUTPUT_DIR}/resource-plan.md"
  )
  set(ros2_zephyr_resource_report
      "${ROS2_ZEPHYR_DEPLOYMENT_OUTPUT_DIR}/resource-report.json"
  )
  set(ros2_zephyr_resource_report_markdown
      "${ROS2_ZEPHYR_DEPLOYMENT_OUTPUT_DIR}/resource-report.md"
  )
  set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ros2_zephyr_resource_tool}"
  )
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}" "${ros2_zephyr_resource_tool}" plan
      --deployment-plan "${ROS2_ZEPHYR_DEPLOYMENT_PLAN}"
      --config "${CMAKE_BINARY_DIR}/zephyr/.config"
      --target "${CONFIG_BOARD_TARGET}"
      --json "${ros2_zephyr_resource_plan}"
      --markdown "${ros2_zephyr_resource_plan_markdown}"
    RESULT_VARIABLE resource_plan_result
    OUTPUT_VARIABLE resource_plan_output
    ERROR_VARIABLE resource_plan_error
  )
  if(NOT resource_plan_result EQUAL 0)
    string(STRIP "${resource_plan_error}" resource_plan_error)
    message(FATAL_ERROR "Resource admission failed: ${resource_plan_error}")
  endif()
  string(STRIP "${resource_plan_output}" resource_plan_output)
  message(STATUS "${resource_plan_output}")
endif()

foreach(flag_variable ROS2_ZEPHYR_C_FLAGS ROS2_ZEPHYR_CXX_FLAGS)
  string(REPLACE "\\" "\\\\" ${flag_variable} "${${flag_variable}}")
  string(REPLACE "\"" "\\\"" ${flag_variable} "${${flag_variable}}")
endforeach()

set(ros2_zephyr_include_directories "${ros_install}/include")
foreach(package_name IN LISTS ros2_zephyr_header_packages)
  list(APPEND ros2_zephyr_include_directories "${ros_install}/include/${package_name}")
endforeach()
file(MAKE_DIRECTORY "${ros2_zephyr_root}" "${ros_install}/include")
foreach(include_directory IN LISTS ros2_zephyr_include_directories)
  file(MAKE_DIRECTORY "${include_directory}")
endforeach()
set(toolchain_template "${ros2_zephyr_root}/zephyr_toolchain.configured.cmake")
configure_file(
  "${CMAKE_CURRENT_LIST_DIR}/../zephyr_toolchain.cmake.in" "${toolchain_template}" @ONLY
)
file(GENERATE OUTPUT "${toolchain_file}" INPUT "${toolchain_template}")

ExternalProject_Add(
  ros2_zephyr_runtime
  SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.."
  BINARY_DIR "${ros_work_root}"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND
    "${CMAKE_COMMAND}" -E env
    "ROS2_ZEPHYR_MODULE_DIR=${CMAKE_CURRENT_LIST_DIR}/.."
    "ROS2_ZEPHYR_DEPS_ROOT=${ROS2_ZEPHYR_DEPS_ROOT}"
    "ROS2_ZEPHYR_WORK_ROOT=${ros_work_root}"
    "ROS2_ZEPHYR_TOOLCHAIN_FILE=${toolchain_file}"
    "ROS2_ZEPHYR_BACKEND_BUILD_SCRIPT=${ros2_zephyr_backend_build_script}"
    "ROS2_ZEPHYR_AR=${CMAKE_AR}"
    "ROS2_ZEPHYR_RANLIB=${CMAKE_RANLIB}"
    "ROS2_ZEPHYR_DOMAIN_ID=${CONFIG_ROS2_ZEPHYR_DOMAIN_ID}"
    "ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS=${ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS}"
    ${ros2_zephyr_backend_build_environment}
    bash "${CMAKE_CURRENT_LIST_DIR}/../build_static_ros.sh"
  INSTALL_COMMAND ""
  BUILD_ALWAYS TRUE
  DEPENDS ${ros2_zephyr_backend_dependencies}
  BUILD_BYPRODUCTS "${ros_library}"
)

add_library(ros2_zephyr_static STATIC IMPORTED GLOBAL)
set_target_properties(
  ros2_zephyr_static
  PROPERTIES IMPORTED_LOCATION "${ros_library}"
             INTERFACE_INCLUDE_DIRECTORIES "${ros2_zephyr_include_directories}"
)
add_dependencies(ros2_zephyr_static ros2_zephyr_runtime)

zephyr_interface_library_named(ros2_zephyr)
if(ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS)
  target_compile_definitions(ros2_zephyr INTERFACE RCLC_ENABLE_ACTIONS=1)
else()
  target_compile_definitions(ros2_zephyr INTERFACE RCLC_ENABLE_ACTIONS=0)
endif()
target_include_directories(
  ros2_zephyr INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../include"
                        ${ros2_zephyr_include_directories}
)
target_link_libraries(
  ros2_zephyr INTERFACE ros2_zephyr_static ${ros2_zephyr_backend_libraries}
)
add_dependencies(ros2_zephyr ros2_zephyr_runtime)
target_link_libraries(app PUBLIC ros2_zephyr)
target_sources(
  app PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../platform/allocator.c"
              ${ros2_zephyr_backend_sources}
)
add_dependencies(app ros2_zephyr_runtime)

if(ROS2_ZEPHYR_DEPLOYMENT_PLAN)
  add_custom_target(
    ros2_zephyr_verify_resources ALL
    COMMAND
      "${Python3_EXECUTABLE}" "${ros2_zephyr_resource_tool}" verify
      --resource-plan "${ros2_zephyr_resource_plan}"
      --build-dir "${CMAKE_BINARY_DIR}"
      --json "${ros2_zephyr_resource_report}"
      --markdown "${ros2_zephyr_resource_report_markdown}"
    DEPENDS "${CMAKE_BINARY_DIR}/zephyr/zephyr.elf"
    BYPRODUCTS
      "${ros2_zephyr_resource_report}"
      "${ros2_zephyr_resource_report_markdown}"
    COMMENT "Verifying linked image against the deployment resource contract"
    VERBATIM
  )
endif()

if(CONFIG_WIFI_ESP32)
  target_sources(app PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../compat/esp32_wifi_random.c")
endif()
