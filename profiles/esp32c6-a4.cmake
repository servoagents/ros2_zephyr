# Include before find_package(Zephyr); networking and the deployment remain application-owned.
set(ROS2_ZEPHYR_CYCLONEDDS_NATIVE_SYNC ON CACHE BOOL "A4 native synchronization" FORCE)
set(ROS2_ZEPHYR_CYCLONEDDS_LOG_BUFFER_SIZE 512 CACHE STRING "A4 log buffer" FORCE)
set(ROS2_ZEPHYR_ESP32C6_TLS_STACKS ON CACHE BOOL "Reserve vendor Wi-Fi TLS backing" FORCE)
list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/esp32c6-a4.conf")
