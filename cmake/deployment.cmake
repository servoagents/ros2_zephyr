# SPDX-License-Identifier: Apache-2.0

function(ros2_zephyr_compile_deployment deployment_file)
  set(ROS2_ZEPHYR_DEPLOYMENT_BACKEND
      "rmw_cyclonedds_c"
      CACHE STRING "RMW backend selected by the deployment compiler"
  )
  set_property(
    CACHE ROS2_ZEPHYR_DEPLOYMENT_BACKEND
    PROPERTY STRINGS rmw_cyclonedds_c rmw_zenoh_pico
  )

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  get_filename_component(deployment_file "${deployment_file}" ABSOLUTE)
  set(generated_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/deployment")
  set(compiler "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/compile_deployment.py")
  set(capabilities_directory "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../capabilities")
  file(MAKE_DIRECTORY "${generated_directory}")
  execute_process(
    COMMAND
      "${Python3_EXECUTABLE}" "${compiler}" "${deployment_file}"
      --backend "${ROS2_ZEPHYR_DEPLOYMENT_BACKEND}"
      --capabilities-dir "${capabilities_directory}"
      --output-dir "${generated_directory}"
    RESULT_VARIABLE compile_result
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
  )
  if(NOT compile_result EQUAL 0)
    string(STRIP "${compile_error}" compile_error)
    message(FATAL_ERROR "Deployment compilation failed: ${compile_error}")
  endif()

  set_property(
    DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${deployment_file}"
    "${compiler}"
    "${capabilities_directory}/${ROS2_ZEPHYR_DEPLOYMENT_BACKEND}.json"
  )
  list(APPEND EXTRA_CONF_FILE "${generated_directory}/generated_ros2_zephyr.conf")
  set(EXTRA_CONF_FILE "${EXTRA_CONF_FILE}" PARENT_SCOPE)
  set(ROS2_ZEPHYR_GENERATED_DEPLOYMENT_DIR "${generated_directory}" PARENT_SCOPE)
endfunction()

function(ros2_zephyr_add_generated_deployment target)
  if(NOT ROS2_ZEPHYR_GENERATED_DEPLOYMENT_DIR)
    message(FATAL_ERROR "ros2_zephyr_compile_deployment must be called first")
  endif()
  target_sources(
    ${target} PRIVATE
    "${ROS2_ZEPHYR_GENERATED_DEPLOYMENT_DIR}/generated_ros_init.c"
  )
  target_include_directories(
    ${target} PRIVATE "${ROS2_ZEPHYR_GENERATED_DEPLOYMENT_DIR}"
  )
endfunction()
