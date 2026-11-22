/* task #61: a function erased to the generic void(*)(void) of a dispatch
   table and recovered with __tzd_fncast of the matching interface is
   called correctly (resolves #22 without weakening the compiler).  */
typedef int (len_fn)(const char *);
static int my_len (const char *s) { int n = 0; while (s[n]) n++; return n; }

int main (void)
{
  void (*g)(void) = (void (*)(void)) my_len;   /* register id(len_fn) */
  len_fn *f = __tzd_fncast (len_fn, g);         /* check + reinterpret */
  return f ("hello") == 5 ? 0 : 1;
}
