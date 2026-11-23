// SPDX-License-Identifier: Apache-2.0
#pragma once

#ifdef CONFIG_APP_RESOURCE_REPORT
void app_resource_report(const char *phase);
#else
static inline void app_resource_report(const char *phase) { (void)phase; }
#endif
