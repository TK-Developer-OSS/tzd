/* task #61: C11 atomics under #pragma tzd atomics (OpenSSL refcount.h
   pattern).  _Atomic scalars keep their representation; the atomic
   operations lower to __atomic builtins, resolved to the correctly typed
   call.  Without the pragma, _Atomic is rejected (err_atomic.c).  */
#pragma tzd atomics
#include <stdatomic.h>
struct rc { _Atomic int val; };
int main (void) {
  struct rc r; r.val = 0;
  int a = atomic_fetch_add_explicit (&r.val, 1, memory_order_relaxed) + 1;
  int b = atomic_load_explicit (&r.val, memory_order_acquire);
  int c = atomic_fetch_sub_explicit (&r.val, 1, memory_order_release) - 1;
  atomic_thread_fence (memory_order_acquire);
  return (a == 1 && b == 1 && c == 0) ? 0 : 1;
}
