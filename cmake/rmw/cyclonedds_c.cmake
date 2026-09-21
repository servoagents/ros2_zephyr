# SPDX-License-Identifier: Apache-2.0

set(ros2_zephyr_backend_name "rmw_cyclonedds_c")
set(ros2_zephyr_middleware_name "cyclonedds")
set(ROS2_ZEPHYR_CYCLONEDDS_SOURCE "" CACHE PATH "Pinned Cyclone DDS source")
set(ROS2_ZEPHYR_HOST_IDLC "" CACHE FILEPATH "Host idlc executable")
set(ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE
    ""
    CACHE PATH "Optional local rmw_cyclonedds_c source for development builds"
)
set(ROS2_ZEPHYR_RMW_SOURCE
    ""
    CACHE PATH "Deprecated alias for ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE"
)
if(NOT "${ROS2_ZEPHYR_RMW_SOURCE}" STREQUAL "" AND
   NOT "${ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE}" STREQUAL "" AND
   NOT "${ROS2_ZEPHYR_RMW_SOURCE}" STREQUAL "${ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE}"
)
  message(FATAL_ERROR "Set only one rmw_cyclonedds_c source override")
endif()
if(NOT "${ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE}" STREQUAL "")
  set(ros2_zephyr_rmw_source "${ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE}")
else()
  set(ros2_zephyr_rmw_source "${ROS2_ZEPHYR_RMW_SOURCE}")
endif()

set(ros2_zephyr_default_cyclonedds_uri
    "<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Sizing><ReceiveBufferSize>8KiB</ReceiveBufferSize><ReceiveBufferChunkSize>4KiB</ReceiveBufferChunkSize></Sizing><Discovery><ParticipantIndex>0</ParticipantIndex><MaxAutoParticipantIndex>0</MaxAutoParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery><Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal></Domain></CycloneDDS>"
)
set(ros2_zephyr_legacy_cyclonedds_uri
    "<CycloneDDS><Domain><General><NetworkInterfaceAddress>127.0.0.1</NetworkInterfaceAddress><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Sizing><ReceiveBufferSize>8KiB</ReceiveBufferSize><ReceiveBufferChunkSize>4KiB</ReceiveBufferChunkSize></Sizing><Discovery><ParticipantIndex>0</ParticipantIndex><MaxAutoParticipantIndex>0</MaxAutoParticipantIndex><Peers><Peer Address=\"127.0.0.1\"/></Peers></Discovery><Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal></Domain></CycloneDDS>"
)
set(ROS2_ZEPHYR_CYCLONEDDS_URI
    "${ros2_zephyr_default_cyclonedds_uri}"
    CACHE STRING "Cyclone DDS XML used by rmw_cyclonedds_c"
)
if("${ROS2_ZEPHYR_CYCLONEDDS_URI}" STREQUAL "${ros2_zephyr_legacy_cyclonedds_uri}")
  set(ROS2_ZEPHYR_CYCLONEDDS_URI
      "${ros2_zephyr_default_cyclonedds_uri}"
      CACHE STRING "Cyclone DDS XML used by rmw_cyclonedds_c" FORCE
  )
endif()

if(NOT IS_DIRECTORY "${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}")
  message(FATAL_ERROR "ROS2_ZEPHYR_CYCLONEDDS_SOURCE must name an existing directory")
endif()
if(NOT EXISTS "${ROS2_ZEPHYR_HOST_IDLC}")
  message(FATAL_ERROR "ROS2_ZEPHYR_HOST_IDLC must name the host idlc executable")
endif()
if(NOT "${ros2_zephyr_rmw_source}" STREQUAL "" AND
   NOT IS_DIRECTORY "${ros2_zephyr_rmw_source}/rmw_cyclonedds_c"
)
  message(FATAL_ERROR "The rmw_cyclonedds_c source override is not a valid repository")
endif()

target_compile_definitions(app PRIVATE __STDC_WANT_LIB_EXT1__=1 _DEFAULT_SOURCE=1)
target_compile_definitions(
  app PRIVATE ROS2_ZEPHYR_RMW_NAME="${ros2_zephyr_backend_name}"
              ROS2_ZEPHYR_MIDDLEWARE_NAME="${ros2_zephyr_middleware_name}"
)

set(cyclonedds_zephyr_workarounds "")
if(KERNEL_VERSION_MAJOR GREATER_EQUAL 4)
  string(APPEND cyclonedds_zephyr_workarounds " -DCYCLONEDDS_ZEPHYR_EXPLICIT_SCHED=1")
endif()
if(KERNEL_VERSION_MAJOR EQUAL 4
   AND KERNEL_VERSION_MINOR EQUAL 4
   AND KERNEL_PATCHLEVEL LESS_EQUAL 2
)
  string(APPEND cyclonedds_zephyr_workarounds
         " -DCYCLONEDDS_ZEPHYR_CONDVAR_TIMEOUT_RELOCK=1"
  )
endif()
string(APPEND ros2_zephyr_common_definitions
       " -D__STDC_WANT_LIB_EXT1__=1 -D_DEFAULT_SOURCE=1"
       " -DCONFIG_MAX_PTHREAD_COUNT=CONFIG_POSIX_THREAD_THREADS_MAX"
       " -DCYCLONEDDS_THREAD_COUNT=${CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_COUNT}"
       " -DCYCLONEDDS_THREAD_STACK_SIZE=${CONFIG_ROS2_ZEPHYR_CYCLONEDDS_THREAD_STACK_SIZE}"
       "${cyclonedds_zephyr_workarounds}"
)
ros2_zephyr_finalize_compiler_flags()

set(cyclonedds_install "${ros2_zephyr_root}/cyclonedds-prefix")
set(cyclonedds_binary "${ros2_zephyr_root}/cyclonedds-build")
set(cyclonedds_library "${cyclonedds_binary}/lib/libddsc.a")
list(APPEND ros2_zephyr_header_packages rosidl_typesupport_cyclonedds_c)
file(MAKE_DIRECTORY "${cyclonedds_install}/include")

ExternalProject_Add(
  cyclonedds_zephyr
  SOURCE_DIR "${ROS2_ZEPHYR_CYCLONEDDS_SOURCE}"
  BINARY_DIR "${cyclonedds_binary}"
  INSTALL_DIR "${cyclonedds_install}"
  BUILD_ALWAYS TRUE
  CMAKE_ARGS -DBUILD_SHARED_LIBS=OFF
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
  BUILD_BYPRODUCTS "${cyclonedds_library}"
)

add_library(cyclonedds_ros2_zephyr_static STATIC IMPORTED GLOBAL)
set_target_properties(
  cyclonedds_ros2_zephyr_static
  PROPERTIES IMPORTED_LOCATION "${cyclonedds_library}"
             INTERFACE_INCLUDE_DIRECTORIES "${cyclonedds_install}/include"
)
add_dependencies(cyclonedds_ros2_zephyr_static cyclonedds_zephyr)

string(REPLACE "\"" "'" ros2_zephyr_cyclonedds_uri "${ROS2_ZEPHYR_CYCLONEDDS_URI}")
list(APPEND ros2_zephyr_backend_build_environment
     "ROS2_ZEPHYR_CYCLONE_PREFIX=${cyclonedds_install}"
     "ROS2_ZEPHYR_HOST_IDLC=${ROS2_ZEPHYR_HOST_IDLC}"
     "ROS2_ZEPHYR_RMW_CYCLONEDDS_C_SOURCE=${ros2_zephyr_rmw_source}"
     "ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES=${CONFIG_ROS2_ZEPHYR_GRAPH_MAX_LOCAL_NODES}"
     "ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE=${CONFIG_ROS2_ZEPHYR_GRAPH_MAX_ENDPOINTS_PER_NODE}"
     "ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS=${CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_PARTICIPANTS}"
     "ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES=${CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_NODES}"
     "ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS=${CONFIG_ROS2_ZEPHYR_GRAPH_CACHE_MAX_ENDPOINTS}"
     "ROS2_ZEPHYR_CYCLONEDDS_URI=${ros2_zephyr_cyclonedds_uri}"
)
list(APPEND ros2_zephyr_backend_dependencies cyclonedds_zephyr)
list(APPEND ros2_zephyr_backend_libraries cyclonedds_ros2_zephyr_static)
list(APPEND ros2_zephyr_backend_sources
     "${CMAKE_CURRENT_LIST_DIR}/../../platform/rmw/cyclonedds_c.c"
)
set(ros2_zephyr_backend_build_script
    "${CMAKE_CURRENT_LIST_DIR}/../../rmw/cyclonedds_c/build_static_ros.sh"
)
