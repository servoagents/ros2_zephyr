# SPDX-License-Identifier: Apache-2.0

set(ROS2_ZEPHYR_DEPS_ROOT
    ""
    CACHE PATH "Prepared ROS 2 Zephyr dependencies"
)
if(NOT IS_DIRECTORY "${ROS2_ZEPHYR_DEPS_ROOT}")
  message(FATAL_ERROR "ROS2_ZEPHYR_DEPS_ROOT must name an existing directory")
endif()

if(NOT DEFINED ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS)
  set(ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS ON)
endif()

zephyr_get_system_include_directories_for_lang_as_string(C zephyr_system_includes)
zephyr_get_include_directories_for_lang_as_string(C zephyr_includes)
zephyr_get_compile_definitions_for_lang_as_string(C zephyr_definitions)
zephyr_get_compile_options_for_lang_as_string(C zephyr_options)
zephyr_get_system_include_directories_for_lang_as_string(CXX zephyr_system_includes_cxx)
zephyr_get_include_directories_for_lang_as_string(CXX zephyr_includes_cxx)
zephyr_get_compile_definitions_for_lang_as_string(CXX zephyr_definitions_cxx)
zephyr_get_compile_options_for_lang_as_string(CXX zephyr_options_cxx)

set(ros2_zephyr_common_definitions "-D_POSIX_C_SOURCE=200809L")
set(ros2_zephyr_root "${CMAKE_CURRENT_BINARY_DIR}/ros2-zephyr")
set(ros_work_root "${ros2_zephyr_root}/ros")
set(ros_install "${ros_work_root}/install")
set(ros_library "${ros_work_root}/libros2_zephyr.a")
set(toolchain_file "${ros2_zephyr_root}/zephyr_toolchain.cmake")

set(ros2_zephyr_header_packages
    builtin_interfaces
    rcl
    rcl_interfaces
    rcl_logging_interface
    rclc
    rcutils
    rmw
    ros2_zephyr_test_msgs
    rosidl_dynamic_typesupport
    rosidl_runtime_c
    rosidl_typesupport_c
    rosidl_typesupport_interface
    rosidl_typesupport_introspection_c
    service_msgs
    std_msgs
    tracetools
    type_description_interfaces
    unique_identifier_msgs
)
if(ROS2_ZEPHYR_RCLC_ENABLE_ACTIONS)
  list(APPEND ros2_zephyr_header_packages action_msgs rcl_action)
endif()

set(ros2_zephyr_backend_build_environment "")
set(ros2_zephyr_backend_dependencies "")
set(ros2_zephyr_backend_libraries "")
set(ros2_zephyr_backend_sources "")

function(ros2_zephyr_finalize_compiler_flags)
  set(ROS2_ZEPHYR_C_FLAGS
      "${zephyr_definitions} ${ros2_zephyr_common_definitions} ${zephyr_system_includes} ${zephyr_includes} ${zephyr_options}"
      PARENT_SCOPE
  )
  set(ROS2_ZEPHYR_CXX_FLAGS
      "${zephyr_definitions_cxx} ${ros2_zephyr_common_definitions} ${zephyr_system_includes_cxx} ${zephyr_includes_cxx} ${zephyr_options_cxx}"
      PARENT_SCOPE
  )
endfunction()
