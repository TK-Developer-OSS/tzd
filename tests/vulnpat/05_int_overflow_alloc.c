/* 05  Integer overflow in an allocation size (CWE-190 -> CWE-680), the
   shape of the OpenSSH challenge-response bug (CVE-2002-0639).

   Shape: an element count from the message is multiplied by the element
   size in 32-bit arithmetic; the product wraps to a small number, the
   allocation succeeds, and the items that follow are stored as if the
   array were as large as the count says.
   0x40000002 * 4 wraps to 8.  The message carries 12 items (48 bytes); the
   8-byte request gets a 24-byte glibc block, so the stores run over the
   next block's header and its first 16 bytes.  Nothing is freed afterwards.

   GCC (any flags):  the neighbour's text changes, the program continues.
                     The arithmetic is unsigned, so it is not undefined
                     behaviour and -ftrapv does not apply either.
   TzD:              the array carries the bounds of the 8 bytes actually
                     allocated; the store to v[2] traps.  */
#include "vp.h"

#define ITEMS_IN_MESSAGE 12

int main(void)
{
    unsigned int count = (unsigned int) VP_RT(0x40000002);
    unsigned int i;
    int *v;
    char *next;

    vp_begin("05_int_overflow_alloc");
#if VP_BAD
    v = malloc(count * (unsigned int) sizeof (int));    /* wraps to 8 */
#else
    if (count > ITEMS_IN_MESSAGE || count > (size_t) -1 / sizeof (int))
        return vp_end();                    /* reject the count */
    v = malloc(count * sizeof (int));
#endif
    next = malloc(24);
    strcpy(next, "neighbour-block");
    for (i = 0; i < ITEMS_IN_MESSAGE && i < count; i++)
        v[i] = 0x41414141;
    vp_reached();
    if (strcmp(next, "neighbour-block") != 0) {
        vp_effect("corrupted", "items stored past the wrapped allocation");
        vp_show("neighbour", next, 16);
    } else
        vp_effect("none", "the next heap block is intact");
    return vp_end();
}
