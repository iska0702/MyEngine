#pragma once
#if defined(__APPLE__)
#include_next <xlocale.h>
#else
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#ifndef __int8_t_defined
#define __int8_t_defined
typedef int8_t   __int8_t;
typedef int16_t  __int16_t;
typedef int32_t  __int32_t;
typedef int64_t  __int64_t;
typedef uint8_t  __uint8_t;
typedef uint16_t __uint16_t;
typedef uint32_t __uint32_t;
typedef uint64_t __uint64_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _LOCALE_T_DEFINED
#define _LOCALE_T_DEFINED
typedef void* locale_t;
#endif

#ifndef _LIBCPP_GET_C_LOCALE
#define _LIBCPP_GET_C_LOCALE ((locale_t)0)
#endif

static inline long long strtoll_l(const char* nptr, char** endptr, int base, locale_t) {
    return strtoll(nptr, endptr, base);
}
static inline unsigned long long strtoull_l(const char* nptr, char** endptr, int base, locale_t) {
    return strtoull(nptr, endptr, base);
}
static inline long double strtold_l(const char* nptr, char** endptr, locale_t) {
    return strtold(nptr, endptr);
}
static inline float strtof_l(const char* nptr, char** endptr, locale_t) {
    return strtof(nptr, endptr);
}
static inline double strtod_l(const char* nptr, char** endptr, locale_t) {
    return strtod(nptr, endptr);
}
static inline int sscanf_l(const char* s, locale_t, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsscanf(s, format, args);
    va_end(args);
    return ret;
}
static inline int snprintf_l(char* s, size_t maxlen, locale_t, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int ret = vsnprintf(s, maxlen, format, args);
    va_end(args);
    return ret;
}
static inline int asprintf_l(char** ret, locale_t, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int r = vasprintf(ret, format, args);
    va_end(args);
    return r;
}

#ifndef LC_COLLATE_MASK
#define LC_COLLATE_MASK  (1 << 0)
#define LC_CTYPE_MASK    (1 << 1)
#define LC_MONETARY_MASK (1 << 2)
#define LC_NUMERIC_MASK  (1 << 3)
#define LC_TIME_MASK     (1 << 4)
#define LC_MESSAGES_MASK (1 << 5)
#define LC_ALL_MASK      (LC_COLLATE_MASK | LC_CTYPE_MASK | LC_MONETARY_MASK | LC_NUMERIC_MASK | LC_TIME_MASK | LC_MESSAGES_MASK)
#endif

#ifndef isascii
#define isascii(c) (((c) & ~0x7F) == 0)
#endif

#ifdef __cplusplus
}
#endif
#endif
