/* tzd-flags: -I.
   The same header: the access past the end of the array traps, as it does
   in any project code (before, a header with the pragma was system code
   and a host pointer there was refused at compile time).  */
#include "lib_gcc_system_header.h"
int main (void)
{
  char buf[4] = { 1, 2, 3, 4 };
  return hdr_get (buf, 4);
}
