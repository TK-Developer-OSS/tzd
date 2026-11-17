/* task #61: atomic ops used as a STATEMENT (stdatomic.h expands them to
   __extension__({...}); OpenSSL tsan_assist.h/drbg.c do this).  cc1tzd must
   parse __extension__ before an expression, not only before a declaration.  */
#pragma tzd atomics
#include <stdatomic.h>
struct s { _Atomic int c; };
void bump (struct s *p) {
  atomic_store_explicit (&p->c, 5, memory_order_relaxed);   /* statement form */
  atomic_fetch_add_explicit (&p->c, 1, memory_order_relaxed);
end:
  (void) p;
}
int main (void) { struct s s; bump (&s); return atomic_load_explicit (&s.c, memory_order_relaxed) == 6 ? 0 : 1; }
