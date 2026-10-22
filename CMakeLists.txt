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

  if(ROS2_ZEPHYR_ESP32C6_TLS_STACKS)
    if(NOT CONFIG_SOC_ESP32C6_HPCORE OR NOT ROS2_ZEPHYR_CYCLONEDDS_NATIVE_SYNC)
      message(FATAL_ERROR "The C6 TLS stack profile requires ESP32-C6 HP and native DDSRT synchronization")
    endif()
    target_sources(app PRIVATE ${CMAKE_CURRENT_LIST_DIR}/platform/esp32c6_wifi_stack.c)
    zephyr_ld_options(-Wl,--wrap=config_get_wifi_task_stack_size)
  endif()
  include(${CMAKE_CURRENT_LIST_DIR}/cmake/runtime.cmake)
endif()
