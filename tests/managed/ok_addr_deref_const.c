/* tzd-flags: -Os -mno-sse */
/* &(&key)->f (the kernel's static_key_enabled in DO_ONCE): *&x folds to x, so
   the address constant has no INDIRECT_REF.  Without SSE (the kernel's
   -mno-sse) GCC puts the pointer record in its constant pool, whose
   hash crashed on it (internal compiler error in tree_to_shwi).  */
typedef struct { int counter; } atomic_t;
struct static_key { atomic_t enabled; };
struct static_key_true { struct static_key key; };
static inline __attribute__((__always_inline__)) int static_key_count (struct static_key *key)
{ return *(const volatile int *) &(&key->enabled)->counter; }
static _Bool wrong (void) { return 0; }
int main (void)
{
  _Bool ret = 0;
  static struct static_key_true once = (struct static_key_true) { .key = { .enabled = { (1) } }, };
  if (__builtin_expect (!!(({ if (!__builtin_types_compatible_p (typeof (*&(&once)->key), struct static_key)) wrong ();
			      static_key_count ((struct static_key *) &(&once)->key) > 0; })), 0))
    ret = 1;
  return !ret;
}
