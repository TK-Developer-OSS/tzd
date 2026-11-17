// SPDX-License-Identifier: MIT
/* TzD strings (libc2d mode).  */
#ifndef _TZD_STRINGS_H
#define _TZD_STRINGS_H
typedef __SIZE_TYPE__ size_t;
int strcasecmp(const char *a, const char *b)            __asm__("tzd_strcasecmp");
int strncasecmp(const char *a, const char *b, size_t n) __asm__("tzd_strncasecmp");
#endif
