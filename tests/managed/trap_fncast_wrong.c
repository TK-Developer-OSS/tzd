/* task #61: recovering a generic function pointer under a different
   interface than it was registered with traps (the contract check).  */
typedef int (len_fn)(const char *);
typedef int (bin_fn)(int, int);
static int my_len (const char *s) { int n = 0; while (s[n]) n++; return n; }

int main (void)
{
  void (*g)(void) = (void (*)(void)) my_len;   /* id(len_fn) */
  bin_fn *f = __tzd_fncast (bin_fn, g);         /* id(bin_fn) != id(len_fn) */
  return f (2, 3);                               /* trap before the call */
}
