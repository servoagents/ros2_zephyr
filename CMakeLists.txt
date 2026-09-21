# SPDX-License-Identifier: Apache-2.0

if(CONFIG_ROS2_ZEPHYR)
  include(ExternalProject)
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/common.cmake)

  if(CONFIG_ROS2_ZEPHYR_RMW_CYCLONEDDS_C)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/rmw/cyclonedds_c.cmake)
  elseif(CONFIG_ROS2_ZEPHYR_RMW_ZENOH_PICO)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/rmw/zenoh_pico.cmake)
  else()
    message(FATAL_ERROR "No ROS 2 Zephyr RMW backend is selected")
  endif()

  include(${CMAKE_CURRENT_LIST_DIR}/cmake/runtime.cmake)
endif()
