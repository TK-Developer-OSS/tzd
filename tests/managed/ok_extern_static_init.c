/* &x[i] of an extern array in a static initializer (the kernel's sysctl
   tables: .extra1 = &sysctl_vals[1]).  The size is known only at link
   time: the startup table reads it from __tzd_size.vals.  */
extern const int vals[];
struct ctl { const char *name; const void *extra1; const void *extra2; };
static struct ctl tab[] = {
  { "a", &vals[1], &vals[2] },
  { "b", vals, 0 },
};
const int vals[] = { 10, 20, 30 };
int main(void)
{
  const int *p = tab[0].extra1, *q = tab[1].extra1, *r = tab[0].extra2;
  if (*p != 20 || q[2] != 30 || *r != 30 || p[-1] != 10) return 1;
  return sizeof tab / sizeof tab[0] != 2;
}
