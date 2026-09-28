# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ddsrt_native_sync C)

get_filename_component(cyclonedds_source "${CMAKE_CURRENT_LIST_DIR}/../../../cyclonedds" ABSOLUTE)
if(NOT EXISTS "${cyclonedds_source}/src/ddsrt/src/sync/zephyr/sync.c")
  message(FATAL_ERROR "Milestone B requires the local cyclonedds/zephyr-native-ddsrt fork")
endif()

target_include_directories(app BEFORE PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/include"
  "${cyclonedds_source}/src/ddsrt/include")
target_sources(app PRIVATE
  src/main.c
  "${cyclonedds_source}/src/ddsrt/src/sync/zephyr/sync.c"
  "${cyclonedds_source}/src/ddsrt/src/time/posix/time.c")
