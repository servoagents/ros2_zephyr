/* SPDX-License-Identifier: Apache-2.0 */

#ifndef THREAD_TLS_QUALIFICATION_EXTERNAL_H_
#define THREAD_TLS_QUALIFICATION_EXTERNAL_H_

#include <stdint.h>

uintptr_t tls_external_address(void);
uint32_t tls_external_read(void);
void tls_external_write(uint32_t value);

#endif
