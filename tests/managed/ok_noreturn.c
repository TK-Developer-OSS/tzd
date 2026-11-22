/* noreturn (attribute and _Noreturn) and do { ... } while (0) ending in
   __builtin_unreachable(): no "control reaches end" (the kernel builds
   with -Werror=return-type: BUG(), panic(), do_exit()).  */
/* tzd-flags: -Werror=return-type */
#include <stdlib.h>
void __attribute__((__noreturn__)) die (int c) { exit (c); }
_Noreturn void die2 (void) { exit (3); }
static int f (int x) { if (x) return 0; die (2); }
static int g (int x) { if (x) return 0; die2 (); }
static int h (int x) { if (x) return 0; do { __builtin_unreachable (); } while (0); }
int main (void)
{
  int n = 0;
  do { n++; } while (0);
  return f (1) + g (1) + h (1) + n - 1;
}
