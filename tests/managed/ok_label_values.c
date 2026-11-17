/* Labels as values and computed goto (GNU C, the kernel's BPF
   interpreter): a static jump table of &&label inside the function; the
   function is marked so -O2 does not clone it away from its labels.  */
static int run (const unsigned char *prog)
{
  static const void *const table[4] = { [0 ... 3] = &&bad, [1] = &&inc, [2] = &&dbl, [3] = &&done };
  int acc = 1;
  const void *next;
#define DISPATCH() do { next = table[*prog++]; goto *next; } while (0)
  DISPATCH ();
inc: acc++; DISPATCH ();
dbl: acc *= 2; DISPATCH ();
bad: return -1;
done: return acc;
}
int main (void)
{
  static const unsigned char p[] = { 1, 2, 1, 3 };
  return run (p) - 5;
}
