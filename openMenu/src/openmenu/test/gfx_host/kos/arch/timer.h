/* Host stand-in for KOS (gfx_host_test): just enough for gfx.c to compile and run on a PC. */
#pragma once
#include <stdint.h>
uint64_t timer_us_gettime64(void);
