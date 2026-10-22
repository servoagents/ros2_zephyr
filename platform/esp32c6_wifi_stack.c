/* ESP32-C6 Wi-Fi stack reservation for compiler TLS. Preserve the vendor execution budget
 * and reserve the linked TLS image, arch frame/alignment/sentinel, and margin.
 * The wrapper changes only the vendor size provider. */
#include <stdint.h>
#include <zephyr/sys/util.h>
extern char __tdata_size[], __tbss_size[], __tdata_align[], __tbss_align[];
extern unsigned __real_config_get_wifi_task_stack_size(void);
unsigned __wrap_config_get_wifi_task_stack_size(void)
{
  unsigned tls = (uintptr_t)__tdata_size + (uintptr_t)__tbss_size;
  unsigned align = MAX((uintptr_t)__tdata_align, (uintptr_t)__tbss_align);
  return ROUND_UP(__real_config_get_wifi_task_stack_size() + tls + align - 1 + 99 + 256, 16);
}
