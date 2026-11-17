/* -ftzd-mpx (experimental): bounds checks by Intel MPX instructions.
   mpx_test ok      in-bounds accesses of a stack array, a heap block and a
                    struct member; prints "mpx ok"
   mpx_test over    writes one byte past a stack array      -> #BR
   mpx_test under   reads one element before a heap block   -> #BR
   mpx_test heap    reads byte 20 of a 16-byte heap block   -> #BR
   mpx_test end     reads the byte just past a heap block   -> #BR
   mpx_test null    reads through a null pointer            -> trap (software)  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct account { char name[8]; long balance; };

__attribute__ ((noinline)) static long sum (const long *p, int n)
{
  long s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

__attribute__ ((noinline)) static void put (char *p, int i, char c)
{
  p[i] = c;
}

int main (int argc, char **argv)
{
  const char *what = argc > 1 ? argv[1] : "ok";
  long v[4] = { 1, 2, 3, 4 };
  struct account a = { "alice", 100 };
  long *h = malloc (4 * sizeof *h);
  char *buf = malloc (16);
  for (int i = 0; i < 4; i++)
    h[i] = 10 * (i + 1);
  memset (buf, 'x', 16);
  if (strcmp (what, "ok") == 0)
    {
      put (a.name, 7, 0);
      if (sum (v, 4) != 10 || sum (h, 4) != 100 || a.balance != 100
	  || buf[15] != 'x')
	return 1;
      puts ("mpx ok");
      return 0;
    }
  fflush (stdout);
  if (strcmp (what, "over") == 0)
    put (a.name, 8, 'Z');
  else if (strcmp (what, "under") == 0)
    return (int) sum (h - 1, 1);
  else if (strcmp (what, "heap") == 0)
    return buf[atoi ("20")];
  else if (strcmp (what, "end") == 0)
    {
      char *e = malloc (16);
      return *(e + 16);
    }
  else if (strcmp (what, "null") == 0)
    {
      long *p = argc > 2 ? v : NULL;
      return (int) *p;
    }
  puts ("mpx_test: not stopped");
  return 2;
}
