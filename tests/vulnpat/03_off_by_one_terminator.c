/* 03  Off-by-one: the terminator written one past the buffer (CWE-193).

   Shape: a hand-written bounded copy that stops at the capacity, then
   writes the terminator at dst[i], which is dst[cap] when the input fills
   the buffer.  One zero byte lands on the next member: here it clears the
   "locked" flag of the record.

   GCC (any flags):  locked is cleared, the program continues.  No libc
                     call is involved, so _FORTIFY_SOURCE has nothing to
                     check; the stack protector guards the return address,
                     not members.
   TzD:              dst carries the bounds of the tag member (8 bytes);
                     the store to dst[8] traps.  */
#include "vp.h"

struct record {
    char tag[8];
    unsigned char locked;
};

static void copy_tag(char *dst, const char *src, int cap)
{
    int i;
#if VP_BAD
    for (i = 0; i < cap && src[i]; i++)
#else
    for (i = 0; i < cap - 1 && src[i]; i++)
#endif
        dst[i] = src[i];
    dst[i] = 0;
}

static const char *const tags[] = { "ABCDEFGH", "ABCDEFGH" };

int main(void)
{
    struct record r;

    vp_begin("03_off_by_one_terminator");
    r.locked = 1;
    copy_tag(r.tag, tags[VP_RT(VP_BAD)], (int) sizeof r.tag);
    vp_reached();
    if (!r.locked)
        vp_effect("corrupted", "locked cleared by the terminator");
    else
        vp_effect("none", "locked intact");
    return vp_end();
}
