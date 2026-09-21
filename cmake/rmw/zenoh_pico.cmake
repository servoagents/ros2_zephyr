# SPDX-License-Identifier: Apache-2.0

set(ros2_zephyr_backend_name "rmw_zenoh_pico")
set(ros2_zephyr_middleware_name "zenoh_pico")
set(ROS2_ZEPHYR_ZENOH_PICO_SOURCE
    "${ROS2_ZEPHYR_DEPS_ROOT}/platform/src/eclipse/zenoh-pico"
    CACHE PATH "Pinned Zenoh-Pico source"
)
set(ROS2_ZEPHYR_MICROCDR_SOURCE
    "${ROS2_ZEPHYR_DEPS_ROOT}/platform/src/eprosima/micro-CDR"
    CACHE PATH "Pinned Micro-CDR source"
)
set(ROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE
    "${ROS2_ZEPHYR_DEPS_ROOT}/target/src/fj-blanco/rmw_zenoh_pico"
    CACHE PATH "Pinned rmw_zenoh_pico source"
)
set(ROS2_ZEPHYR_ZENOH_ROUTER_IPV4
    "${CONFIG_ROS2_ZEPHYR_ZENOH_ROUTER_IPV4}"
    CACHE STRING "Zenoh router IPv4 address"
)
set(ROS2_ZEPHYR_ZENOH_ROUTER_PORT
    "${CONFIG_ROS2_ZEPHYR_ZENOH_ROUTER_PORT}"
    CACHE STRING "Zenoh router TCP port"
)
if(NOT ROS2_ZEPHYR_ZENOH_ROUTER_IPV4 MATCHES
   "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "ROS2_ZEPHYR_ZENOH_ROUTER_IPV4 must be an IPv4 address")
endif()
if(NOT ROS2_ZEPHYR_ZENOH_ROUTER_PORT MATCHES "^[1-9][0-9]*$" OR
   ROS2_ZEPHYR_ZENOH_ROUTER_PORT GREATER 65535)
  message(FATAL_ERROR "ROS2_ZEPHYR_ZENOH_ROUTER_PORT must be in the range 1..65535")
endif()

foreach(required_source
        ROS2_ZEPHYR_ZENOH_PICO_SOURCE
        ROS2_ZEPHYR_MICROCDR_SOURCE
        ROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE)
  if(NOT IS_DIRECTORY "${${required_source}}")
    message(FATAL_ERROR "${required_source} must name an existing directory")
  endif()
endforeach()

target_compile_definitions(
  app PRIVATE ROS2_ZEPHYR_RMW_NAME="${ros2_zephyr_backend_name}"
              ROS2_ZEPHYR_MIDDLEWARE_NAME="${ros2_zephyr_middleware_name}"
)

string(APPEND ros2_zephyr_common_definitions
       " -DZENOH_ZEPHYR=1"
       " -DCONFIG_ZENOH_PICO_THREADS_NUM=4"
       " -I${CMAKE_CURRENT_LIST_DIR}/../../include"
)
ros2_zephyr_finalize_compiler_flags()

set(microcdr_install "${ros2_zephyr_root}/microcdr-prefix")
set(microcdr_binary "${ros2_zephyr_root}/microcdr-build")
set(microcdr_library "${microcdr_install}/lib/libmicrocdr.a")
file(MAKE_DIRECTORY "${microcdr_install}/include")

ExternalProject_Add(
  microcdr_zephyr
  SOURCE_DIR "${ROS2_ZEPHYR_MICROCDR_SOURCE}"
  BINARY_DIR "${microcdr_binary}"
  INSTALL_DIR "${microcdr_install}"
  CMAKE_ARGS -DUCDR_SUPERBUILD=OFF
             -DUCDR_ISOLATED_INSTALL=OFF
             -DUCDR_BUILD_TESTS=OFF
             -DUCDR_BUILD_EXAMPLES=OFF
             -DUCDR_PIC=OFF
             -DBUILD_SHARED_LIBS=OFF
             -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
             -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
             -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
             -DCMAKE_C_FLAGS=${ROS2_ZEPHYR_C_FLAGS}
             -DCMAKE_SYSTEM_NAME=Generic
             -DCMAKE_BUILD_TYPE=MinSizeRel
  DEPENDS zephyr_interface zephyr_generated_headers
  BUILD_BYPRODUCTS "${microcdr_library}"
)

set(zenoh_pico_stage "${ros2_zephyr_root}/zenoh-pico-src")
set(zenoh_pico_install "${ros2_zephyr_root}/zenoh-pico-prefix")
set(zenoh_pico_binary "${ros2_zephyr_root}/zenoh-pico-build")
set(zenoh_pico_library "${zenoh_pico_install}/lib/libzenohpico.a")
file(MAKE_DIRECTORY "${zenoh_pico_install}/include")

ExternalProject_Add(
  zenoh_pico_zephyr
  SOURCE_DIR "${zenoh_pico_stage}"
  BINARY_DIR "${zenoh_pico_binary}"
  INSTALL_DIR "${zenoh_pico_install}"
  DOWNLOAD_COMMAND
    "${CMAKE_COMMAND}" -E remove_directory "${zenoh_pico_stage}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${ROS2_ZEPHYR_ZENOH_PICO_SOURCE}" "${zenoh_pico_stage}"
  PATCH_COMMAND
    "${CMAKE_COMMAND}" -DZENOH_PICO_SOURCE=${zenoh_pico_stage} -P
    "${CMAKE_CURRENT_LIST_DIR}/../../patches/zenoh-pico-zephyr-4.4.cmake"
  CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF
             -DBUILD_TESTING=OFF
             -DPACKAGING=OFF
             -DZP_PLATFORM=zephyr
             -DZ_FEATURE_LINK_TCP=1
             -DZ_FEATURE_LINK_UDP_UNICAST=0
             -DZ_FEATURE_LINK_UDP_MULTICAST=0
             -DZ_FEATURE_SCOUTING=0
             -DZ_FEATURE_MULTICAST_TRANSPORT=0
             -DZ_FEATURE_TCP_NODELAY=0
             -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
             -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
             -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
             -DCMAKE_C_FLAGS=${ROS2_ZEPHYR_C_FLAGS}
             -DCMAKE_SYSTEM_NAME=Generic
             -DCMAKE_BUILD_TYPE=MinSizeRel
  DEPENDS zephyr_interface zephyr_generated_headers
  BUILD_BYPRODUCTS "${zenoh_pico_library}"
)

add_library(microcdr_ros2_zephyr_static STATIC IMPORTED GLOBAL)
set_target_properties(
  microcdr_ros2_zephyr_static
  PROPERTIES IMPORTED_LOCATION "${microcdr_library}"
             INTERFACE_INCLUDE_DIRECTORIES "${microcdr_install}/include"
)
add_dependencies(microcdr_ros2_zephyr_static microcdr_zephyr)

add_library(zenoh_pico_ros2_zephyr_static STATIC IMPORTED GLOBAL)
set_target_properties(
  zenoh_pico_ros2_zephyr_static
  PROPERTIES IMPORTED_LOCATION "${zenoh_pico_library}"
             INTERFACE_INCLUDE_DIRECTORIES "${zenoh_pico_install}/include"
)
add_dependencies(zenoh_pico_ros2_zephyr_static zenoh_pico_zephyr)

list(APPEND ros2_zephyr_header_packages
     micro_ros_msgs
     rmw_zenoh_pico
     rosidl_typesupport_microxrcedds_c
)
list(APPEND ros2_zephyr_backend_build_environment
     "ROS2_ZEPHYR_MICROCDR_PREFIX=${microcdr_install}"
     "ROS2_ZEPHYR_ZENOH_PICO_PREFIX=${zenoh_pico_install}"
     "ROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE=${ROS2_ZEPHYR_RMW_ZENOH_PICO_SOURCE}"
     "ROS2_ZEPHYR_ZENOH_ROUTER_IPV4=${ROS2_ZEPHYR_ZENOH_ROUTER_IPV4}"
     "ROS2_ZEPHYR_ZENOH_ROUTER_PORT=${ROS2_ZEPHYR_ZENOH_ROUTER_PORT}"
     "ROS2_ZEPHYR_ZENOH_MAX_LIVELINESS_LENGTH=${CONFIG_ROS2_ZEPHYR_ZENOH_MAX_LIVELINESS_LENGTH}"
)
list(APPEND ros2_zephyr_backend_dependencies microcdr_zephyr zenoh_pico_zephyr)
list(APPEND ros2_zephyr_backend_libraries
     zenoh_pico_ros2_zephyr_static
     microcdr_ros2_zephyr_static
)
set(ros2_zephyr_backend_build_script
    "${CMAKE_CURRENT_LIST_DIR}/../../rmw/zenoh_pico/build_static_ros.sh"
)
