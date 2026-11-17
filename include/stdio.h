// SPDX-License-Identifier: MIT
/* TzD stdio (Managed C view).

   In Managed C every `char *` is a managed pointer object, so these
   functions are bound to libc2d's managed implementations (tzd_*), which take
   the pointer object as-is.  Only what libc2d implements is declared; the
   rest of stdio (FILE streams, sprintf, ...) comes with libc2d's stream
   layer.  Self-contained: no other header is needed.  */
#ifndef _TZD_STDIO_H
#define _TZD_STDIO_H

typedef __SIZE_TYPE__ size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif
#define EOF (-1)

int printf(const char *fmt, ...) __asm__("tzd_printf");
int puts(const char *s) __asm__("tzd_puts");
int putchar(int c) __asm__("tzd_putchar");

#endif
