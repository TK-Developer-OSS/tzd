/* tzd-flags: -std=gnu17
   C99 inline (task #71, gnulib): an `inline` function whose declarations
   are all `inline`, none `extern`, is an inline definition, and this unit
   provides no external definition of it; a later declaration without
   `inline` makes it external after all.  Two such units link without a
   multiple definition.  */
#include "lib_c99_inline.h"
int main (void)
{
  return twice (4) == 8 ? 0 : 1;
}
