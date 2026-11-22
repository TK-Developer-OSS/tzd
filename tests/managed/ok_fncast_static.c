/* task #61: a function erased to the generic void(*)(void) in a STATIC
   dispatch table is registered at startup (section tzd_fninit) and then
   recovered with __tzd_fncast -- the OpenSSL OSSL_DISPATCH pattern.  */
typedef int (len_fn)(const char *);
static int my_len (const char *s) { int n = 0; while (s[n]) n++; return n; }

struct disp { int id; void (*fn)(void); };
static const struct disp table[] = {
  { 1, (void (*)(void)) my_len },
  { 0, 0 },
};

int main (void)
{
  len_fn *f = __tzd_fncast (len_fn, table[0].fn);
  return f ("abcd") == 4 ? 0 : 1;
}
