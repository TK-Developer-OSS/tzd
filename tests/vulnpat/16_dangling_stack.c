/* 16  Pointer to a local that outlives its function (CWE-562).

   Shape: a helper hands its caller the address of its own local buffer
   through an out parameter (a plain "return buf" is caught by GCC's
   warnings, the out parameter is not); the caller keeps the pointer, and
   the next call reuses that stack.
   The same helper is called twice from the same caller, so both calls get
   the same frame and the first call's buffer is the second call's buffer:
   the first result now reads the second call's text.

   GCC (any flags):  the first greeting reads "ZZZZZ", the program
                     continues.
   TzD:              leaving the helper wipes buf (twice) and drops its
                     bounds, but the pointer stored in the caller's slot
                     keeps them; see the README.  */
#include "vp.h"

__attribute__((noinline)) static void make_greeting(char **out, const char *text)
{
#if VP_BAD
    char buf[16];
#else
    char *buf = malloc(16);                 /* the fix: outlives the call */
#endif
    strcpy(buf, text);
    *out = buf;
}

int main(void)
{
    char *first = NULL, *second = NULL;
    char c;

    vp_begin("16_dangling_stack");
    make_greeting(&first, "hello");
    make_greeting(&second, "ZZZZZ");
    c = first[VP_RT(0)];
    vp_reached();
    if (c == 'Z')
        vp_effect("confused", "the first result reads the second call's buffer");
    else if (c == 'h')
        vp_effect("none", "the first result still reads its own text");
    else
        vp_effect("none", "the first result reads other bytes");
    return vp_end();
}
