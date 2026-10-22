/* SPDX-License-Identifier: Apache-2.0 */

#include "tls_external.h"

static __thread volatile uint32_t external_value = 0x5a5a5a5aU;

uintptr_t tls_external_address(void) { return (uintptr_t)&external_value; }

uint32_t tls_external_read(void) { return external_value; }

void tls_external_write(uint32_t value) { external_value = value; }
