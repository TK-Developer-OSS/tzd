/* tzd-flags: --host lib_managed_override.c
   `#pragma tzd managed_override error` (task #71): a libc-named function
   the program defines in Managed C takes the managed calling convention
   at every call, also the variable arguments (gnulib error.c).  */
#pragma tzd managed_override error
#include <sys/cdefs.h>
#undef __va_arg_pack
#undef __va_arg_pack_len
#include <error.h>
#include <stdio.h>
int main (void) { error (0, 0, "n=%d s=%s", 7, "x"); return 0; }
