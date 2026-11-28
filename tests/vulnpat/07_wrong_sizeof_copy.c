/* 07  Copy sized by the wrong object (CWE-805 / CWE-131).

   Shape: memcpy(dst, src, sizeof src) where src is the larger buffer.  The
   destination is a member, so the excess lands on the next member: the
   role string becomes the second half of the source.

   GCC -O0:            role reads "admin", the program continues.
   GCC _FORTIFY_SOURCE=2: memcpy is checked against the size of the whole
                       enclosing object (16 bytes from name to the end of
                       the struct), not the member, so it is not caught.
   TzD:                name carries its own bounds (8 bytes); memcpy checks
                       the destination range and traps.  */
#include "vp.h"

struct account {
    char name[8];
    char role[8];
};

static const char src_name[16] = "operator" "admin\0\0";

int main(void)
{
    struct account a;

    vp_begin("07_wrong_sizeof_copy");
    memset(&a, 0, sizeof a);
    strcpy(a.role, "user");
#if VP_BAD
    memcpy(a.name, src_name, VP_RT(sizeof src_name));
#else
    memcpy(a.name, src_name, VP_RT(sizeof a.name));
#endif
    vp_reached();
    if (strcmp(a.role, "user") != 0) {
        vp_effect("corrupted", "role rewritten by the oversized copy");
        vp_show("role", a.role, 7);
    } else
        vp_effect("none", "role intact");
    return vp_end();
}
