cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES "${CMAKE_CURRENT_SOURCE_DIR}/../../../zephyr")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ros2_zephyr_phase5_loopback C CXX)

target_sources(app PRIVATE src/main.c)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
