/* Kernel expressions (task #42): a file-scope static array declared
   before its size (kernel/cpu.c, a local size symbol defined at the end
   of the unit); c ? void-call : 0 (GNU, void result, pgtable.h);
   function pointer + integer (GNU, bytes, bpf).  */
static struct step { int n; } states[];
static struct step *get (int s) { return states + s; }
static void clear (int *p) { *p = 0; }
static int hook (int x) { return x; }
static struct step states[] = { { 1 }, { 2 }, { 3 } };
int main (void)
{
  int v = 5;
  1 ? clear (&v) : 0;
  int (*f) (int) = hook;
  void *a = f + 16, *b = 16 + f;
  if (a != b || (char *) 0 == 0 && a == (void *) hook) return 1;
  return get (2)->n + v - 3;
}
