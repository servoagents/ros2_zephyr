# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED ZENOH_PICO_SOURCE OR NOT IS_DIRECTORY "${ZENOH_PICO_SOURCE}")
  message(FATAL_ERROR "ZENOH_PICO_SOURCE must name the staged Zenoh-Pico source")
endif()

set(system_source "${ZENOH_PICO_SOURCE}/src/system/zephyr/system.c")
file(READ "${system_source}" system_contents)

set(memory_before [=[#include "zenoh-pico/system/platform.h"

/*------------------ Random ------------------*/]=])
set(memory_after [=[#include "zenoh-pico/system/platform.h"

#include <ros2_zephyr/allocator.h>

/*------------------ Random ------------------*/]=])
string(REPLACE "${memory_before}" "${memory_after}" system_contents "${system_contents}")

set(allocator_before [=[void *z_malloc(size_t size) { return k_malloc(size); }

void *z_realloc(void *ptr, size_t size) {
    // k_realloc not implemented in Zephyr
    return NULL;
}

void z_free(void *ptr) { k_free(ptr); }]=])
set(allocator_after [=[static void *z_allocator_state(void) {
    return ros2_zephyr_allocator_state(ROS2_ZEPHYR_ALLOCATION_MIDDLEWARE);
}

void *z_malloc(size_t size) { return ros2_zephyr_allocate(size, z_allocator_state()); }

void *z_realloc(void *ptr, size_t size) { return ros2_zephyr_reallocate(ptr, size, z_allocator_state()); }

void z_free(void *ptr) { ros2_zephyr_deallocate(ptr, z_allocator_state()); }]=])
string(REPLACE "${allocator_before}" "${allocator_after}" system_contents "${system_contents}")

set(attr_before [=[    rc = pthread_create(task, &tmp, fun, arg);
    int attr_rc = pthread_attr_destroy(&tmp);
    if (rc != 0) {
        _z_zephyr_task_release_stack(stack_slot);
        _z_report_system_error(rc);
        _Z_ERROR_RETURN(_Z_ERR_SYSTEM_GENERIC);
    }
    if (attr_rc != 0) {
        _z_report_system_error(attr_rc);
    }]=])
set(attr_after [=[    rc = pthread_create(task, &tmp, fun, arg);
    // Zephyr owns the caller-provided stack after pthread_create(). Destroying
    // the copied attributes would try to free that static stack.
    if (rc != 0) {
        _z_zephyr_task_release_stack(stack_slot);
        _z_report_system_error(rc);
        _Z_ERROR_RETURN(_Z_ERR_SYSTEM_GENERIC);
    }]=])
string(REPLACE "${attr_before}" "${attr_after}" system_contents "${system_contents}")

if(system_contents MATCHES "k_malloc\(size\)" OR
   system_contents MATCHES "int attr_rc = pthread_attr_destroy")
  message(FATAL_ERROR "Zenoh-Pico Zephyr compatibility substitutions did not apply")
endif()
file(WRITE "${system_source}" "${system_contents}")

set(platform_profile "${ZENOH_PICO_SOURCE}/cmake/platforms/zephyr.cmake")
file(READ "${platform_profile}" platform_contents)
if(NOT platform_contents MATCHES "set\\(CHECK_THREADS OFF\\)")
  file(APPEND "${platform_profile}" "\nset(CHECK_THREADS OFF)\n")
endif()

set(root_cmake "${ZENOH_PICO_SOURCE}/CMakeLists.txt")
file(READ "${root_cmake}" cmake_contents)
string(REPLACE
  "elseif(CMAKE_SYSTEM_NAME MATCHES \"Generic\")\n    add_compile_options(-pipe -O3)"
  "elseif(CMAKE_SYSTEM_NAME MATCHES \"Generic\")\n    add_compile_options(-pipe)"
  cmake_contents "${cmake_contents}"
)
string(REPLACE
  "elseif(CMAKE_SYSTEM_NAME MATCHES \"Generic\")\n    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes -pipe -g -O0)"
  "elseif(CMAKE_SYSTEM_NAME MATCHES \"Generic\")\n    add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wmissing-prototypes -pipe)"
  cmake_contents "${cmake_contents}"
)
file(WRITE "${root_cmake}" "${cmake_contents}")
