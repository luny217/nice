/* This file is part of the Nice GLib ICE library */

#ifndef _CRC32_H
#define _CRC32_H


#ifdef _WIN32
#include "win32_common.h"
#else
#include <stdint.h>
#include <stdbool.h>
#endif

#include <stdlib.h>

typedef struct
{
    uint8_t * buf;
    size_t len;
} crc_data;


uint32_t stun_crc32(const crc_data * data, size_t n, bool wlm2009_stupid_crc32_typo);

#endif /* _CRC32_H */
