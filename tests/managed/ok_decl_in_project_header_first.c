/* tzd-flags: --host
   A libc function first declared in a project header (gnulib's stdio.h
   declares free and malloc for their attributes) and then in the system
   header is the host's function, so a call of it is redirected to libc2d
   like any other (task #71): malloc and free of one block agree.  */
#include "lib_proj_decl.h"
#include <stdlib.h>
int main (void)
{
  char *p = malloc (8);
  p[7] = 1;
  free (p);
  return 0;
}
