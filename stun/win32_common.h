/* This file is part of the Nice GLib ICE library */

#ifndef _WIN32_COMMON_H
#define _WIN32_COMMON_H

#include "config.h"
#include <sys/types.h>

/* 7.18.1.1  Exact-width integer types */
typedef signed char int8_t;
typedef unsigned char   uint8_t;
typedef short  int16_t;
typedef unsigned short  uint16_t;
typedef int  int32_t;
typedef unsigned   uint32_t;
typedef long long  int64_t;
typedef unsigned long long   uint64_t;

/* Using the [S]SIZE_T definitions from:
 * http://msdn.microsoft.com/en-gb/library/windows/desktop/aa383751%28v=vs.85%29.aspx#SSIZE_T */
#ifndef HAVE_SIZE_T
#if defined(_WIN64)
typedef unsigned __int64 size_t;
#else
//typedef unsigned long size_t;
#endif
#endif  /* !HAVE_SSIZE_T */
#ifndef HAVE_SSIZE_T
#if defined(_WIN64)
typedef signed __int64 ssize_t;
#else
typedef signed long ssize_t;
#endif
#endif  /* !HAVE_SSIZE_T */

typedef uint8_t bool;
#define true 1
#define false 0


#endif /* _WIN32_COMMON_H */
