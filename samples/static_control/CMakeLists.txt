# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

if(BOARD MATCHES "^esp32")
  foreach(required_variable WIFI_SSID WIFI_PSK ROS_PEER_IP)
    if("$ENV{${required_variable}}" STREQUAL "")
      message(FATAL_ERROR "${required_variable} must be set for an ESP32 build")
    endif()
  endforeach()
  set(ros_peer_ip "$ENV{ROS_PEER_IP}")
  if(NOT ros_peer_ip MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "ROS_PEER_IP must be an IPv4 address")
  endif()
  set(ROS2_ZEPHYR_CYCLONEDDS_URI
      "<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Sizing><ReceiveBufferSize>8KiB</ReceiveBufferSize><ReceiveBufferChunkSize>4KiB</ReceiveBufferChunkSize></Sizing><Discovery><ParticipantIndex>0</ParticipantIndex><MaxAutoParticipantIndex>1</MaxAutoParticipantIndex><Peers><Peer Address='${ros_peer_ip}'/></Peers></Discovery><Threads><Thread Name='recv'><Scheduling><Class>timeshare</Class><Priority>8</Priority></Scheduling></Thread><Thread Name='tev'><Scheduling><Class>timeshare</Class><Priority>4</Priority></Scheduling></Thread><Thread Name='dq.user'><Scheduling><Class>timeshare</Class><Priority>4</Priority></Scheduling></Thread><Thread Name='dq.builtins'><Scheduling><Class>timeshare</Class><Priority>7</Priority></Scheduling></Thread><Thread Name='gc'><Scheduling><Class>timeshare</Class><Priority>4</Priority></Scheduling></Thread></Threads><Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal></Domain></CycloneDDS>"
      CACHE STRING "Cyclone DDS XML for static_control" FORCE
  )
endif()

list(APPEND ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../..")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ros2_zephyr_static_control C CXX)

if(NOT CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C)
  message(FATAL_ERROR "static_control currently requires rmw_cyclonedds_c")
endif()

target_sources(app PRIVATE src/main.c src/control.c src/plant.c)
target_include_directories(app PRIVATE src)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

if(CONFIG_WIFI)
  function(escape_c_string output input)
    string(REPLACE "\\" "\\\\" escaped "${input}")
    string(REPLACE "\"" "\\\"" escaped "${escaped}")
    string(REPLACE "\n" "\\n" escaped "${escaped}")
    string(REPLACE "\r" "\\r" escaped "${escaped}")
    set(${output} "${escaped}" PARENT_SCOPE)
  endfunction()

  escape_c_string(wifi_ssid_c "$ENV{WIFI_SSID}")
  escape_c_string(wifi_psk_c "$ENV{WIFI_PSK}")
  set(generated_directory "${CMAKE_CURRENT_BINARY_DIR}/generated")
  file(MAKE_DIRECTORY "${generated_directory}")
  configure_file(src/wifi_credentials.h.in "${generated_directory}/wifi_credentials.h" @ONLY)
  target_include_directories(app PRIVATE "${generated_directory}")
endif()
