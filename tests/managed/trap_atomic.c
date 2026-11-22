/* task #61: an atomic access out of the object's bounds traps -- atomics are
   bounds-checked like any other access (the pointer is checked for
   sizeof(*p) bytes before the __atomic builtin).  */
#pragma tzd atomics
#include <stdatomic.h>
int main (void) {
  int a[2];
  volatile int off = 2;
  _Atomic int *p = (_Atomic int *) (a + off);   /* one int past the array */
  return atomic_fetch_add_explicit (p, 1, memory_order_relaxed);
}
