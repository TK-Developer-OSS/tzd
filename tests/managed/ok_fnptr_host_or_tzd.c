/* tzd-flags: --host
   `wild ? fnmatch : own_function`: a host function and a TzD function in
   one conditional (gnulib exclude.c, task #71): the result takes the TzD
   function pointer type, the host function gets its thunk.  */
#include <fnmatch.h>
#include <string.h>
static int nowild (char const *pattern, char const *f, int options)
{ (void) options; return strcmp (pattern, f); }
int pick (int wild, char const *p, char const *f)
{
  int (*matcher) (char const *, char const *, int) = wild ? fnmatch : nowild;
  return matcher (p, f, 0);
}
int main (void) { return pick (1, "a*", "abc") == 0 && pick (0, "a", "a") == 0 && pick (0, "a*", "abc") != 0 ? 0 : 1; }
