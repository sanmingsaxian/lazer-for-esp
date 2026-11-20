/* Arduino pgmspace compatibility shim for ESP-IDF.
 * On ESP32 the flash is memory-mapped: PROGMEM is a no-op and the _P
 * variants are plain memory operations. libmad only needs these two. */
#ifndef LAZER_PGMSPACE_SHIM_H
#define LAZER_PGMSPACE_SHIM_H

#include <string.h>

#define PROGMEM
#define PSTR(s) (s)
#define memcpy_P memcpy

#endif
