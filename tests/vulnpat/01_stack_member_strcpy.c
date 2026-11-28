/* 01  Stack buffer overflow into the next member (CWE-121).

   Shape: a fixed-size name field filled by strcpy from input that is never
   measured.  The bytes past the field land on the member after it, here
   the privilege flag, without leaving the struct.
   The name is 19 characters: with its terminator the copy writes exactly
   the 4 bytes of is_admin and nothing beyond the struct, so the GCC build
   runs on silently.

   GCC -O0:            is_admin becomes nonzero, the program continues.
   GCC _FORTIFY_SOURCE=2: strcpy into a member of known size can be caught
                       (__strcpy_chk); a hand-written loop is not (see 03).
   TzD:                the array member carries its own bounds; strcpy
                       stops at its end and traps.  */
#include "vp.h"

struct login {
    char user[16];
    int is_admin;
};

static const char *const names[] = {
    "mallory",                  /* fits */
    "mallory-the-admin!!",      /* 19 characters + terminator = 20 bytes */
};

int main(void)
{
    struct login l;
    const char *name = names[VP_RT(VP_BAD)];

    vp_begin("01_stack_member_strcpy");
    l.is_admin = 0;
#if VP_BAD
    strcpy(l.user, name);
#else
    if (strlen(name) >= sizeof l.user)
        return vp_end();                    /* reject the long name */
    strcpy(l.user, name);
#endif
    vp_reached();
    if (l.is_admin)
        vp_effect("corrupted", "is_admin rewritten by the name copy");
    else
        vp_effect("none", "is_admin intact");
    return vp_end();
}
