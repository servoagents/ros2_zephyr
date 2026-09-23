# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(static_control_plant_test C)

target_sources(app PRIVATE test_plant.c ../../src/plant.c)
target_include_directories(app PRIVATE ../../src)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
