/* TzD string (Managed C / TzD view, libc2d mode).  Bound to libc2d's checked
   tzd_* implementations, which take the pointer objects as-is: every byte
   they touch is checked against the bounds.  */
#ifndef _TZD_STRING_H
#define _TZD_STRING_H
typedef __SIZE_TYPE__ size_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
void  *memchr(const void *s, int c, size_t n)          __asm__("tzd_memchr");
int    memcmp(const void *a, const void *b, size_t n)  __asm__("tzd_memcmp");
void  *memcpy(void *dst, const void *src, size_t n)    __asm__("tzd_memcpy");
void  *memmove(void *dst, const void *src, size_t n)   __asm__("tzd_memmove");
void  *memset(void *dst, int c, size_t n)              __asm__("tzd_memset");
size_t strlen(const char *s)                           __asm__("tzd_strlen");
char  *strcpy(char *dst, const char *src)              __asm__("tzd_strcpy");
char  *strncpy(char *dst, const char *src, size_t n)   __asm__("tzd_strncpy");
char  *strcat(char *dst, const char *src)              __asm__("tzd_strcat");
char  *strncat(char *dst, const char *src, size_t n)   __asm__("tzd_strncat");
int    strcmp(const char *a, const char *b)            __asm__("tzd_strcmp");
int    strncmp(const char *a, const char *b, size_t n) __asm__("tzd_strncmp");
char  *strchr(const char *s, int c)                    __asm__("tzd_strchr");
char  *strrchr(const char *s, int c)                   __asm__("tzd_strrchr");
char  *strstr(const char *s, const char *sub)          __asm__("tzd_strstr");
#endif
