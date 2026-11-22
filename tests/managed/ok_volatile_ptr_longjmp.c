/* tzd-flags: --host
   A `T *volatile` local keeps the value assigned between setjmp and
   longjmp (C 7.13.2.1): it has a memory slot, not registers.  */
#include <setjmp.h>
#include <stdio.h>
static jmp_buf jb;
static int a = 1, b = 2;
__attribute__((noinline)) static void jump(void) { longjmp(jb, 1); }
__attribute__((noinline)) static int f(void)
{
    int *volatile p = &a;
    if (setjmp(jb))
        return *p;
    p = &b;
    jump();
    return 0;
}
int main(void)
{
    if (f() != 2) return 1;
    puts("ok");
    return 0;
}
