// SPDX-License-Identifier: Apache-2.0

#include <stdlib.h>

#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

int rand(void) { return (int)(sys_rand32_get() & RAND_MAX); }

void srand(unsigned int seed) { ARG_UNUSED(seed); }
