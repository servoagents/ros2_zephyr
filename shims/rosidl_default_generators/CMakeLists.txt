# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(rosidl_default_generators NONE)
find_package(ament_cmake REQUIRED)
ament_export_dependencies(rosidl_core_generators action_msgs service_msgs)
ament_package()
