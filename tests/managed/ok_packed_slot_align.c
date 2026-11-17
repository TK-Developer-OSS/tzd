/* packed types whose pointers all sit at 8-byte offsets keep GCC's
   layout (alignment 1): the kernel's objpool_slot, lsm_static_calls_table
   and perf_raw_frag (28 bytes).  Variables of them are placed 8-byte
   aligned, so their slots are.  */
struct slot { unsigned int head, tail, last, mask; void *entries[]; } __attribute__((packed));
struct call { void *key; void *fn; };
struct table { struct call a[2]; struct call b[3]; } __attribute__((packed));
struct frag { union { struct frag *next; unsigned long pad; }; void *copy; void *data; unsigned int size; } __attribute__((packed));
struct record { struct frag frag; unsigned int size; };
_Static_assert (sizeof (struct slot) == 16, "slot size");
_Static_assert (__builtin_offsetof (struct slot, entries) == 16, "slot offset");
_Static_assert (sizeof (struct table) == 80 && _Alignof (struct table) == 1, "table");
_Static_assert (sizeof (struct frag) == 28 && sizeof (struct record) == 32, "frag");
static struct table t;
int main (void)
{
  int x = 1;
  char c = 0;
  struct table u[2];
  struct frag f;
  struct record r;
  u[1].b[2].fn = &x;
  t.a[1].key = &x;
  f.data = &x;
  r.frag.data = &c;
  return *(int *) u[1].b[2].fn + *(int *) t.a[1].key + *(int *) f.data
	 + *(char *) r.frag.data - 3;
}
