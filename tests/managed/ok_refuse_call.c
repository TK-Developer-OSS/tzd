/* tzd-flags: --host
   `#pragma tzd refuse_call pthread_create 11` (task #71): the call is not
   made; its arguments are evaluated and it yields 11 (EAGAIN).  */
#pragma tzd returns_twice
#pragma tzd refuse_call pthread_create 11
#include <pthread.h>
static void *worker (void *a) { return a; }
int main (void)
{
  pthread_t t;
  int n = 0;
  int r = pthread_create (&t, 0, worker, &n);
  return r == 11 ? 0 : 1;
}
