/* tzd-flags: -I.
   `#pragma GCC system_header` in a header outside the system directories
   is a no-op (task #71, gnulib): the header's inline function is TzD
   code, its pointer checked.  Reading p[n] past the end traps through
   the header's own function.  */
#include "lib_gcc_system_header.h"
int main (void)
{
  char buf[4] = { 1, 2, 3, 4 };
  return hdr_get (buf, 3) == 4 ? 0 : 1;
}
