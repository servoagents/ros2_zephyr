# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

set(ROS2_ZEPHYR_WIFI_ROLE "sub" CACHE STRING "ESP32 Wi-Fi role: pub or sub")
set_property(CACHE ROS2_ZEPHYR_WIFI_ROLE PROPERTY STRINGS pub sub)
if(NOT ROS2_ZEPHYR_WIFI_ROLE STREQUAL "pub" AND
   NOT ROS2_ZEPHYR_WIFI_ROLE STREQUAL "sub"
)
  message(FATAL_ERROR "ROS2_ZEPHYR_WIFI_ROLE must be pub or sub")
endif()

foreach(required_variable WIFI_SSID WIFI_PSK ROS_PEER_IP)
  if("$ENV{${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} must be set in the environment")
  endif()
endforeach()

set(wifi_ssid "$ENV{WIFI_SSID}")
set(wifi_psk "$ENV{WIFI_PSK}")
set(ros_peer_ip "$ENV{ROS_PEER_IP}")
if(NOT ros_peer_ip MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "ROS_PEER_IP must be an IPv4 address")
endif()

set(ROS2_ZEPHYR_CYCLONEDDS_URI
    "<CycloneDDS><Domain><General><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize></General><Sizing><ReceiveBufferSize>8KiB</ReceiveBufferSize><ReceiveBufferChunkSize>4KiB</ReceiveBufferChunkSize></Sizing><Discovery><ParticipantIndex>0</ParticipantIndex><MaxAutoParticipantIndex>0</MaxAutoParticipantIndex><Peers><Peer Address='${ros_peer_ip}'/></Peers></Discovery><Threads><Thread Name='recv'><Scheduling><Class>timeshare</Class><Priority>8</Priority></Scheduling></Thread><Thread Name='tev'><Scheduling><Class>timeshare</Class><Priority>4</Priority></Scheduling></Thread><Thread Name='dq.user'><Scheduling><Class>timeshare</Class><Priority>4</Priority></Scheduling></Thread><Thread Name='dq.builtins'><Scheduling><Class>timeshare</Class><Priority>7</Priority></Scheduling></Thread><Thread Name='gc'><Scheduling><Class>timeshare</Class><Priority>4</Priority></Scheduling></Thread></Threads><Internal><MultipleReceiveThreads>false</MultipleReceiveThreads></Internal></Domain></CycloneDDS>"
    CACHE STRING "Cyclone DDS XML for the Wi-Fi sample" FORCE
)

function(escape_c_string output input)
  string(REPLACE "\\" "\\\\" escaped "${input}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  string(REPLACE "\n" "\\n" escaped "${escaped}")
  string(REPLACE "\r" "\\r" escaped "${escaped}")
  set(${output} "${escaped}" PARENT_SCOPE)
endfunction()

escape_c_string(wifi_ssid_c "${wifi_ssid}")
escape_c_string(wifi_psk_c "${wifi_psk}")
set(generated_directory "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${generated_directory}")
configure_file(src/wifi_credentials.h.in "${generated_directory}/wifi_credentials.h" @ONLY)

list(APPEND ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../..")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ros2_zephyr_wifi C CXX)

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE "${generated_directory}")
target_compile_definitions(app PRIVATE ROS2_ZEPHYR_WIFI_ROLE_${ROS2_ZEPHYR_WIFI_ROLE}=1)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
