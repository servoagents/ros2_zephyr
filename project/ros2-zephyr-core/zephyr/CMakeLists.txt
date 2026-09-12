# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ROS2_ZEPHYR)
  if(NOT CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C)
    message(FATAL_ERROR "Phase 5 requires CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C")
  endif()

  include(ExternalProject)

  # Cyclone DDS requests the optional C11 bounds-checking typedefs. Define
  # this before any application/system header first includes sys/_types.h.
  zephyr_compile_definitions(
    __STDC_WANT_LIB_EXT1__=1
    _DEFAULT_SOURCE=1)

  set(ROS2_ZEPHYR_LAB_ROOT "" CACHE PATH "ros2-zephyr-lab checkout")
  set(ROS2_ZEPHYR_DEPS_ROOT "" CACHE PATH "Prepared Phase 5 dependencies")
  set(ROS2_ZEPHYR_CYCLONEDDS_SOURCE "" CACHE PATH "Pinned Cyclone DDS source")
  set(ROS2_ZEPHYR_HOST_IDLC "" CACHE FILEPATH "Host idlc executable")
  set(ros2_zephyr_default_cyclonedds_uri
    "<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Sizing><ReceiveBufferSize>8KiB</ReceiveBufferSize><ReceiveBufferChunkSize>4KiB</ReceiveBufferChunkSize></Sizing><Discovery><ParticipantIndex>0</ParticipantIndex><MaxAutoParticipantIndex>0</MaxAutoParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery><Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal></Domain></CycloneDDS>")
  set(ros2_zephyr_legacy_cyclonedds_uri
    "<CycloneDDS><Domain><General><NetworkInterfaceAddress>127.0.0.1</NetworkInterfaceAddress><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Sizing><ReceiveBufferSize>8KiB</ReceiveBufferSize><ReceiveBufferChunkSize>4KiB</ReceiveBufferChunkSize></Sizing><Discovery><ParticipantIndex>0</ParticipantIndex><MaxAutoParticipantIndex>0</MaxAutoParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery><Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal></Domain></CycloneDDS>")
  set(ROS2_ZEPHYR_CYCLONEDDS_URI "${ros2_zephyr_default_cyclonedds_uri}"
    CACHE STRING "Cyclone DDS XML used by the statically built RMW")
  # Migrate only our obsolete default. Preserve intentional user overrides.
  if("${ROS2_ZEPHYR_CYCLONEDDS_URI}" STREQUAL
     "${ros2_zephyr_legacy_cyclonedds_uri}")
    set(ROS2_ZEPHYR_CYCLONEDDS_URI "${ros2_zephyr_default_cyclonedds_uri}"
      CACHE STRING "Cyclone DDS XML used by the statically built RMW" FORCE)
  endif()

  foreach(required_path
      ROS2_ZEPHYR_LAB_ROOT
      ROS2_ZEPHYR_DEPS_ROOT
      ROS2_ZEPHYR_CYCLONEDDS_SOURCE)
    if(NOT IS_DIRECTORY "${${required_path}}")
      message(FATAL_ERROR "${required_path} must name an existing directory")
    endif()
  endforeach()
  if(NOT EXISTS "${ROS2_ZEPHYR_HOST_IDLC}")
    message(FATAL_ERROR "ROS2_ZEPHYR_HOST_IDLC must name the host idlc executable")
  endif()

  zephyr_get_system_include_directories_for_lang_as_string(C zephyr_system_includes)
  zephyr_get_include_directories_for_lang_as_string(C zephyr_includes)
  zephyr_get_compile_definitions_for_lang_as_string(C zephyr_definitions)
  zephyr_get_compile_options_for_lang_as_string(C zephyr_options)
  zephyr_get_system_include_directories_for_lang_as_string(CXX zephyr_system_includes_cxx)
  zephyr_get_include_directories_for_lang_as_string(CXX zephyr_includes_cxx)
  zephyr_get_compile_definitions_for_lang_as_string(CXX zephyr_definitions_cxx)
  zephyr_get_compile_options_for_lang_as_string(CXX zephyr_options_cxx)

  set(cyclonedds_zephyr_workarounds "")
  if(KERNEL_VERSION_MAJOR GREATER_EQUAL 4)
    string(APPEND cyclonedds_zephyr_workarounds
      " -DCYCLONEDDS_ZEPHYR_EXPLICIT_SCHED=1")
  endif()
  if(KERNEL_VERSION_MAJOR EQUAL 4 AND KERNEL_VERSION_MINOR EQUAL 4 AND
     KERNEL_PATCHLEVEL EQUAL 0)
    string(APPEND cyclonedds_zephyr_workarounds
      " -DCYCLONEDDS_ZEPHYR_CONDVAR_TIMEOUT_RELOCK=1")
  endif()
  set(ros2_zephyr_common_definitions
    "-DCONFIG_MAX_PTHREAD_COUNT=CONFIG_POSIX_THREAD_THREADS_MAX -DCYCLONEDDS_THREAD_COUNT=${CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_COUNT} -DCYCLONEDDS_THREAD_STACK_SIZE=${CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_STACK_SIZE}${cyclonedds_zephyr_workarounds} -D_POSIX_C_SOURCE=200809L")
  set(ROS2_ZEPHYR_C_FLAGS
    "${zephyr_definitions} ${ros2_zephyr_common_definitions} ${zephyr_system_includes} ${zephyr_includes} ${zephyr_options}")
  set(ROS2_ZEPHYR_CXX_FLAGS
    "${zephyr_definitions_cxx} ${ros2_zephyr_common_definitions} ${zephyr_system_includes_cxx} ${zephyr_includes_cxx} ${zephyr_options_cxx}")
  set(ROS2_ZEPHYR_C_COMPILER "${CMAKE_C_COMPILER}")
  set(ROS2_ZEPHYR_CXX_COMPILER "${CMAKE_CXX_COMPILER}")
  set(ROS2_ZEPHYR_AR "${CMAKE_AR}")
  set(ROS2_ZEPHYR_RANLIB "${CMAKE_RANLIB}")
  foreach(flag_variable ROS2_ZEPHYR_C_FLAGS ROS2_ZEPHYR_CXX_FLAGS)
    string(REPLACE "\\" "\\\\" ${flag_variable} "${${flag_variable}}")
    string(REPLACE "\"" "\\\"" ${flag_variable} "${${flag_variable}}")
  endforeach()

  set(phase5_root "${CMAKE_CURRENT_BINARY_DIR}/ros2-zephyr")
  set(cyclonedds_install "${phase5_root}/cyclonedds-prefix")
  set(cyclonedds_binary "${phase5_root}/cyclonedds-build")
  set(cyclonedds_library "${cyclonedds_binary}/lib/libddsc.a")
  set(ros_work_root "${phase5_root}/ros")
  set(ros_install "${ros_work_root}/install")
  set(ros_library "${ros_work_root}/libros2_zephyr.a")
  set(toolchain_file "${phase5_root}/zephyr_toolchain.cmake")
  set(ros2_zephyr_header_packages
    action_msgs
    builtin_interfaces
    phase4_test_msgs
    rcl
    rcl_action
    rcl_interfaces
    rcl_logging_interface
    rclc
    rcutils
    rmw
    rosidl_dynamic_typesupport
    rosidl_runtime_c
    rosidl_typesupport_c
    rosidl_typesupport_cyclonedds_c
    rosidl_typesupport_interface
    rosidl_typesupport_introspection_c
    service_msgs
    std_msgs
    tracetools
    type_description_interfaces
    unique_identifier_msgs)
  set(ros2_zephyr_include_directories "${ros_install}/include")
  foreach(package_name IN LISTS ros2_zephyr_header_packages)
    list(APPEND ros2_zephyr_include_directories
      "${ros_install}/include/${package_name}")
  endforeach()
  file(MAKE_DIRECTORY
    "${phase5_root}"
    "${cyclonedds_install}/include"
    "${ros_install}/include")
  foreach(include_directory IN LISTS ros2_zephyr_include_directories)
    file(MAKE_DIRECTORY "${include_directory}")
  endforeach()
  set(toolchain_template "${phase5_root}/zephyr_toolchain.configured.cmake")
  configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/zephyr_toolchain.cmake.in"
    "${toolchain_template}"
    @ONLY)
  file(GENERATE OUTPUT "${toolchain_file}" INPUT "${toolchain_template}")

  # CMake transports this value through an ExternalProject environment and
  # then a compiler definition. XML permits single-quoted attributes, which
  # avoids prematurely ending the generated C string literal.
  string(REPLACE "\"" "'" ros2_zephyr_cyclonedds_uri
    "${ROS2_ZEPHYR_CYCLONEDDS_URI}")

  ExternalProject_Add(cyclonedds_phase5
    SOURCE_DIR "${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}"
    BINARY_DIR "${cyclonedds_binary}"
    INSTALL_DIR "${cyclonedds_install}"
    BUILD_ALWAYS TRUE
    CMAKE_ARGS
      -DBUILD_SHARED_LIBS=OFF
      -DBUILD_EXAMPLES=OFF
      -DBUILD_TESTING=OFF
      -DBUILD_DDSPERF=OFF
      -DBUILD_IDLC=OFF
      -DENABLE_SECURITY=OFF
      -DENABLE_SSL=OFF
      -DENABLE_SOURCE_SPECIFIC_MULTICAST=OFF
      -DENABLE_IPV6=OFF
      -DENABLE_ICEORYX=OFF
      -DENABLE_SHM=OFF
      -DENABLE_TCP=OFF
      -DENABLE_TYPELIB=OFF
      -DENABLE_TYPE_DISCOVERY=OFF
      -DENABLE_TOPIC_DISCOVERY=OFF
      -DENABLE_QOS_PROVIDER=OFF
      -DENABLE_LTO=OFF
      -DWITH_ZEPHYR=ON
      -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
      -DCMAKE_C_FLAGS=${ROS2_ZEPHYR_C_FLAGS}
      -DCMAKE_SYSTEM_NAME=Generic
      -DCMAKE_BUILD_TYPE=MinSizeRel
    DEPENDS zephyr_interface zephyr_generated_headers
    BUILD_BYPRODUCTS "${cyclonedds_library}")

  ExternalProject_Add(ros2_phase5
    SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}"
    BINARY_DIR "${ros_work_root}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
      "${CMAKE_COMMAND}" -E env
      "PHASE5_MODULE_DIR=${CMAKE_CURRENT_LIST_DIR}"
      "PHASE5_LAB_ROOT=${ROS2_ZEPHYR_LAB_ROOT}"
      "PHASE5_DEPS_ROOT=${ROS2_ZEPHYR_DEPS_ROOT}"
      "PHASE5_WORK_ROOT=${ros_work_root}"
      "PHASE5_TOOLCHAIN_FILE=${toolchain_file}"
      "PHASE5_CYCLONE_PREFIX=${cyclonedds_install}"
      "PHASE5_HOST_IDLC=${ROS2_ZEPHYR_HOST_IDLC}"
      "PHASE5_AR=${CMAKE_AR}"
      "PHASE5_RANLIB=${CMAKE_RANLIB}"
      "PHASE5_DOMAIN_ID=${CONFIG_ROS2_ZEPHYR_DOMAIN_ID}"
      "PHASE5_CYCLONEDDS_URI=${ros2_zephyr_cyclonedds_uri}"
      bash "${CMAKE_CURRENT_LIST_DIR}/build_static_ros.sh"
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    DEPENDS cyclonedds_phase5
    BUILD_BYPRODUCTS "${ros_library}")

  add_library(ros2_zephyr_static STATIC IMPORTED GLOBAL)
  set_target_properties(ros2_zephyr_static PROPERTIES
    IMPORTED_LOCATION "${ros_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${ros2_zephyr_include_directories}")
  add_dependencies(ros2_zephyr_static ros2_phase5)

  add_library(cyclonedds_phase5_static STATIC IMPORTED GLOBAL)
  set_target_properties(cyclonedds_phase5_static PROPERTIES
    IMPORTED_LOCATION "${cyclonedds_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${cyclonedds_install}/include")
  add_dependencies(cyclonedds_phase5_static cyclonedds_phase5)

  zephyr_interface_library_named(ros2_zephyr)
  target_include_directories(ros2_zephyr INTERFACE
    ${ros2_zephyr_include_directories})
  target_link_libraries(ros2_zephyr INTERFACE
    ros2_zephyr_static cyclonedds_phase5_static)
  add_dependencies(ros2_zephyr ros2_phase5)
  target_link_libraries(app PUBLIC ros2_zephyr)
  add_dependencies(app ros2_phase5)
endif()
