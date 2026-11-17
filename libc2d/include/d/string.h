// SPDX-License-Identifier: MIT
#ifndef D_STRING_H
#define D_STRING_H
#include <d/pointer.h>

tzd_size_t tzd_strlen(tzd_ptr s);            /* traps if not terminated in range */
tzd_ptr    tzd_memcpy(tzd_ptr dst, tzd_ptr src, tzd_size_t n);
tzd_ptr    tzd_strcpy(tzd_ptr dst, tzd_ptr src);


/* Functions that "return a pointer into s" return a tzd_ptr with the same
   bounds; "not found" is the null tzd_ptr (base == 0).  */
tzd_ptr tzd_memchr(tzd_ptr s, int c, tzd_size_t n);
tzd_ptr tzd_memrchr(tzd_ptr s, int c, tzd_size_t n);
tzd_ptr tzd_rawmemchr(tzd_ptr s, int c);
int   tzd_memcmp(tzd_ptr a, tzd_ptr b, tzd_size_t n);
tzd_ptr tzd_memmove(tzd_ptr dst, tzd_ptr src, tzd_size_t n);
tzd_ptr tzd_memset(tzd_ptr dst, int c, tzd_size_t n);
tzd_ptr tzd_strchr(tzd_ptr s, int c);
tzd_ptr tzd_strrchr(tzd_ptr s, int c);
tzd_ptr tzd_strstr(tzd_ptr s, tzd_ptr sub);
int   tzd_strcmp(tzd_ptr a, tzd_ptr b);
int   tzd_strncmp(tzd_ptr a, tzd_ptr b, tzd_size_t n);
int   tzd_strcasecmp(tzd_ptr a, tzd_ptr b);
int   tzd_strncasecmp(tzd_ptr a, tzd_ptr b, tzd_size_t n);
tzd_ptr tzd_strncpy(tzd_ptr dst, tzd_ptr src, tzd_size_t n);
tzd_ptr tzd_strncat(tzd_ptr dst, tzd_ptr src, tzd_size_t n);
tzd_ptr tzd_strcat(tzd_ptr dst, tzd_ptr src);

#endif
