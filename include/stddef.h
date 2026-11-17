// SPDX-License-Identifier: MIT
/* TzD stddef (Managed C / TzD view, libc2d mode).  */
#ifndef _TZD_STDDEF_H
#define _TZD_STDDEF_H
typedef __SIZE_TYPE__ size_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(T, m) __builtin_offsetof(T, m)
#endif
