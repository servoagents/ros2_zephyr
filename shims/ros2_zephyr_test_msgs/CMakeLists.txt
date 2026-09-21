# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(ros2_zephyr_test_msgs C CXX)

find_package(ament_cmake REQUIRED)
find_package(rosidl_default_generators REQUIRED)

rosidl_generate_interfaces(
  ${PROJECT_NAME}
  "msg/UInt32.msg"
  "msg/FixedArray.msg"
  "msg/NestedFixed.msg"
)

ament_export_dependencies(rosidl_default_runtime)
ament_package()
