/* 02  Heap buffer overflow into the next allocation (CWE-122).

   Shape: a length taken from the message, a buffer allocated with a fixed
   size; memcpy trusts the length.  The bytes past the buffer reach the
   next heap block.
   glibc places two fresh 24-byte requests 32 bytes apart (16-byte chunk
   alignment, 8-byte size header), so a 40-byte copy rewrites the header
   of the second block and its first 8 bytes.  Neither block is freed
   afterwards (the rewritten header would abort free()).

   GCC (any flags):  the neighbour's text changes, the program continues.
                     _FORTIFY_SOURCE cannot see the size of a malloc'd
                     buffer through a plain pointer here.
   TzD:              memcpy checks the destination's bounds (24 bytes)
                     and traps before writing.  */
#include "vp.h"

static const char msg[40] = "AAAAAAAAAAAAAAAAAAAAAAAAhdrhdrhdOVERRIDE";

int main(void)
{
    char *a, *b;
    size_t len = VP_RT(VP_BAD ? 40 : 24);   /* length field of the message */

    vp_begin("02_heap_neighbor_overflow");
    a = malloc(24);
    b = malloc(24);
    strcpy(b, "neighbour");
#if VP_BAD
    memcpy(a, msg, len);
#else
    memcpy(a, msg, len < 24 ? len : 24);
#endif
    vp_reached();
    if (strcmp(b, "neighbour") != 0) {
        vp_effect("corrupted", "the next heap block was rewritten");
        vp_show("neighbour", b, 8);
    } else
        vp_effect("none", "the next heap block is intact");
    return vp_end();
}
