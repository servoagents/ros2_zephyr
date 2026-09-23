# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(static_control_policy_test C)

target_sources(app PRIVATE src/main.c ../../src/control.c ../../src/plant.c)
target_include_directories(app PRIVATE ../../src)
target_compile_definitions(app PRIVATE STATIC_CONTROL_TEST)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
