/* tzd-flags: -std=gnu17
   `__attribute__ ((__gnu_inline__)) extern inline` is an inline-only
   definition even in C99 mode (task #71: gnulib's gettext.h fallbacks of
   gettext, dgettext, ... must not be emitted: every unit would define
   them).  Without the attribute `extern inline` is the external
   definition.  */
__attribute__ ((__always_inline__, __gnu_inline__))
extern inline const char *only_inline (const char *s) { return s; }
extern inline int emitted (int x) { return x + 1; }
int main (void)
{
  return only_inline ("a")[0] == 'a' && emitted (1) == 2 ? 0 : 1;
}
