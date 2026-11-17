// SPDX-License-Identifier: MIT
/* C side of process startup.  */
#include <d/pointer.h>
#include "sys.h"

extern int main(int argc, char **argv, char **envp);

void tzd_start_main(long *sp) __attribute__((noreturn, used));
/* Linked in only when a unit was compiled with -ftzd-mpx (mpx.c).  */
extern void tzd_mpx_init(void) __attribute__((weak));

void tzd_start_main(long *sp)
{
    int argc = (int) sp[0];
    char **argv = (char **) (sp + 1);
    /* -ftzd-mpx programs: enable MPX or stop, before any managed code
       (with the host libc this is done from .preinit_array).  */
    if (tzd_mpx_init)
        tzd_mpx_init();
    tzd_sys_exit(main(argc, argv, argv + argc + 1));
}
