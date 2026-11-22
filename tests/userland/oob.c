/* A command that makes a pointer error on purpose (task #42), compiled
 * with TzD: the access traps and the TzD runtime prints a short dump of
 * it (tzd_trap_bounds), then the process exits with status 134.
 *
 *   oob [n]     writes byte n of an 8-byte array inside a struct (default
 *               12: past the array, into the next member)
 *   oob heap    reads byte 20 of a 16-byte malloc'd buffer
 *   oob null    reads through a null pointer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct account
{
  char name[8];
  long balance;
};

int main (int argc, char **argv)
{
  const char *what = argc > 1 ? argv[1] : "12";
  if (strcmp (what, "heap") == 0)
    {
      char *buf = malloc (16);
      memset (buf, 'x', 16);
      int i = 20;
      printf ("oob: reading byte %d of a 16-byte heap buffer\n", i);
      fflush (stdout);
      return buf[i];
    }
  if (strcmp (what, "null") == 0)
    {
      long *p = argc > 2 ? (long *) 0 : NULL;
      printf ("oob: reading through a null pointer\n");
      fflush (stdout);
      return (int) *p;
    }
  struct account a = { "alice", 100 };
  int n = atoi (what);
  printf ("oob: writing name[%d] of an 8-byte name (balance follows)\n", n);
  fflush (stdout);
  a.name[n] = '!';
  printf ("oob: balance %ld (index %d is inside the name)\n", a.balance, n);
  return 0;
}
