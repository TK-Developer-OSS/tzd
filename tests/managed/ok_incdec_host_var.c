/* tzd-flags: --host
   `optarg++` / `--optarg` on a host variable that has a bounds rule
   (task #71): read through the rule, stepped, stored back; the result
   keeps the string's bounds, so reading inside it works and past its end
   traps.  */
#include <stdio.h>
#include <string.h>
extern char *optarg;
int main (void)
{
  static char buf[] = "-5xyz";
  optarg = buf + 2;
  char *a = optarg++;          /* buf + 2 is "xyz": a is that, optarg moves to "yz" */
  if (*a != 'x' || *optarg != 'y') return 1;
  --optarg;
  optarg--;                    /* now at buf + 1: "5xyz" */
  if (*optarg != '5' || strlen (optarg) != 4) return 2;
  puts ("ok");
  return 0;
}
