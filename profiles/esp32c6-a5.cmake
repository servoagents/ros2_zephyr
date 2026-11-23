# Include before find_package(Zephyr). A5 = A4 with a smaller per-thread footprint.
# See docs/esp32c6-a5.md for the single-thread restriction and measurements.
set(ROS2_ZEPHYR_CYCLONEDDS_NATIVE_SYNC ON CACHE BOOL "A5 native synchronization" FORCE)
set(ROS2_ZEPHYR_CYCLONEDDS_LOG_BUFFER_SIZE 512 CACHE STRING "A5 log buffer" FORCE)
set(ROS2_ZEPHYR_ESP32C6_TLS_STACKS ON CACHE BOOL "Reserve vendor Wi-Fi TLS backing" FORCE)
set(ROS2_ZEPHYR_RCUTILS_SHARED_ERROR_STATE ON CACHE BOOL "A5: one rcl/rmw thread" FORCE)
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/esp32c6-a5.conf")
