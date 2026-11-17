/* What a function leaves on the stack is destroyed when it returns (task
   #69, tzdstack): a later function's uninitialized locals, which take the
   same stack bytes, do not find the earlier function's values -- not in
   its variables and not in the words around them (saved registers,
   spilled values, the return address of its callees).  */
#include <stdio.h>
#define SECRET 0x5ec2e75ec2e75ec2L

__attribute__ ((noinline)) static long mix (long a, long b) { return a ^ (b << 1); }

__attribute__ ((noinline)) static long leave_secret (long seed)
{
  volatile long s[16];
  long r = seed;
  for (int i = 0; i < 16; i++)
    s[i] = SECRET;
  for (int i = 0; i < 16; i++)
    r = mix (r, s[i]);
  return r;
}

__attribute__ ((noinline)) static int peek (void)
{
  volatile long u[40];          /* not initialized: the stack as it is */
  int hits = 0;
  for (int i = 0; i < 40; i++)
    if (u[i] == SECRET)
      hits++;
  return hits;
}

int main (void)
{
  long r = leave_secret (1);
  int hits = peek ();
  if (hits != 0)
    {
      printf ("the secret is still on the stack (%d words)\n", hits);
      return 1;
    }
  printf ("ok %d\n", (int) (r & 1));
  return 0;
}
