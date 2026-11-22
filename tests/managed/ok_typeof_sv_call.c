/* typeof of a call that passes a struct holding pointers by value (the
   kernel's alloc_hooks(memdup_sockptr(...))): the temporary made in the
   unevaluated operand is not queued for the block-exit wipe (it is never
   declared; GCC crashed on the reference).  */
#include <stdlib.h>
#include <string.h>
typedef struct { union { void *kernel; void *user; }; _Bool is_kernel; } sockptr_t;
static inline sockptr_t mk (void *p) { sockptr_t s = { .kernel = p, .is_kernel = 1 }; return s; }
static void *dup_noprof (sockptr_t src, unsigned long len) { void *p = malloc (len); memcpy (p, src.kernel, len); return p; }
#define alloc_hooks(_do) ({ typeof(_do) _res = _do; _res; })
#define dup(...) alloc_hooks(dup_noprof(__VA_ARGS__))
int main (void)
{
  char buf[4] = "abc";
  sockptr_t opt = mk (buf);
  char *p = dup (opt, 4);
  int r = strcmp (p, "abc");
  free (p);
  return r;
}
