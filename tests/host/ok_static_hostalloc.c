/* tzd-flags: -static
   realpath and tempnam in a static program: libc2d's wrappers must pull
   the host functions from libc.a (a weak reference would not, and the
   call would trap; PHP 3's file upload in the PoC guest).  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main (void)
{
  char *t = tempnam (NULL, "tzd");
  if (!t || strncmp (t, "/", 1) != 0)
    return 1;
  free (t);
  char *r = realpath ("/", NULL);
  if (!r || strcmp (r, "/") != 0)
    return 2;
  free (r);
  return 0;
}
