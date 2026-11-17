/* Kernel declarations (task #42):
   - __alignof__ of a flexible array member (crypto.h);
   - [static N] in a parameter array declarator (selection.h);
   - ms_abi on a function pointer member and in a grouped declarator
     with the attribute first (efi.h's __efiapi): calls through them use
     the Microsoft convention on both sides;
   - ((T *)0)->a[0] in typeof (container_of's type check, dcache.c).  */
#define __efiapi __attribute__((ms_abi))
struct ctx { int n; void *crt[] __attribute__((aligned(16))); };
static int first (const unsigned char xy[static 2]) { return xy[0] + xy[1]; }
static long __efiapi add (long a, long b, long c, long d, long e) { return a + b + c + d + e; }
struct table { long (__efiapi *op) (long, long, long, long, long); };
static long call (long (__efiapi *fn) (long, long, long, long, long)) { return fn (1, 2, 3, 4, 5); }
struct name { int len; unsigned char name[]; };
int main (void)
{
  struct ctx *c = 0;
  unsigned char xy[2] = { 3, 4 };
  struct table t = { add };
  typeof (((struct name *) 0)->name[0]) ch = 'a';
  if (__alignof__ (c->crt) != 16 || first (xy) != 7) return 1;
  if (t.op (1, 2, 3, 4, 5) != 15 || call (add) != 15) return 2;
  return ch != 'a' || sizeof ch != 1;
}
