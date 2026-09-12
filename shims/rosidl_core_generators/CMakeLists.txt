# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(rosidl_core_generators NONE)
find_package(ament_cmake REQUIRED)
ament_export_dependencies(
  rosidl_cmake rosidl_generator_c rosidl_typesupport_c rosidl_typesupport_cyclonedds_c
)
ament_package()
