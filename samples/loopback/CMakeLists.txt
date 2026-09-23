# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/deployment.cmake")
ros2_zephyr_compile_deployment("${CMAKE_CURRENT_SOURCE_DIR}/deployment.json")

list(APPEND ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../..")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ros2_zephyr_loopback C CXX)

target_sources(app PRIVATE src/main.c)
ros2_zephyr_add_generated_deployment(app)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
