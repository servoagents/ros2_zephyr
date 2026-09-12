# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)
project(rosidl_core_runtime NONE)
find_package(ament_cmake REQUIRED)
ament_export_dependencies(rosidl_runtime_c rosidl_typesupport_c)
ament_package()
