/* libc2d stdio (hand-lowered).  User-level names puts/printf/... will map
   onto these once the compiler supports pointer objects.  */
#ifndef D_STDIO_H
#define D_STDIO_H
#include <d/pointer.h>

int tzd_putchar(int c);
int tzd_puts(tzd_ptr s);
int tzd_printf(tzd_ptr fmt, ...);   /* %d %u %x %c %s(tzd_ptr) %% */

#endif
