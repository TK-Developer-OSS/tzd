/* TzD directives for building coreutils 9.11 under TzD (task #71),
   force-included in every file of the TzD build
   (tests/coreutils/build_tzd.sh), so the exceptions coreutils needs are
   stated in one auditable place; the default elsewhere stays strict.

   - returns_twice (gnulib's headers declare setjmp-like functions with
     the attribute; coreutils never calls one).  */
#pragma tzd returns_twice

/* - no __va_arg_pack: glibc's <error.h> then declares error () and
     error_at_line () out of line only.  Its inline wrappers (bits/error.h,
     which forward the variable arguments of a caller with __va_arg_pack)
     are code in a system header, where a host pointer of unknown length
     (the format string) is refused (task #24), and they exist for
     _FORTIFY-style checking that TzD does itself.  */
/* (<sys/cdefs.h> pulls in <features.h>, which fixes the feature macros
   for the whole unit: _GNU_SOURCE, which config.h defines too, first.)  */
#ifndef _GNU_SOURCE
# define _GNU_SOURCE 1
#endif
#include <sys/cdefs.h>
#undef __va_arg_pack
#undef __va_arg_pack_len

/* - no threads (task #71): libc2d's bounds table and wipes are not
     thread-safe, and a thread start routine called by the host gets raw
     pointers it cannot check (task #24).  pthread_create is refused with
     EAGAIN (11), as when the system has no more threads; sort (the only
     user) then sorts in the calling thread.  */
#pragma tzd refuse_call pthread_create 11

/* - gnulib's error and error_at_line (lib/error.c) replace glibc's and are
     compiled here: callers must pass managed pointers to them, whatever
     <error.h> says (task #71).  */
#pragma tzd managed_override error error_at_line
