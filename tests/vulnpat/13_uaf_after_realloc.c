/* 13  Dangling pointer after realloc moves the buffer (CWE-416).

   Shape: a parser keeps a pointer into its buffer, grows the buffer with
   realloc, and keeps using the old pointer.  When realloc cannot grow in
   place it moves the data and frees the old block, which the next
   allocation of that size receives.
   A block allocated right after the buffer stops realloc from growing in
   place, so the move is certain; glibc then hands the old 16-byte block to
   the next 16-byte request.

   GCC (any flags):  the write through the old pointer lands in the new
                     owner's block, the program continues.
   TzD:              the old block is released by realloc (wiped, bounds
                     dropped) and never handed out again; the write through
                     the old pointer is expected to stop (see the README for
                     how it stops today).  */
#include "vp.h"

int main(void)
{
    char *buf, *blocker, *field, *other;

    vp_begin("13_uaf_after_realloc");
    buf = malloc(16);
    blocker = malloc(16);                   /* forces realloc to move */
    strcpy(buf, "key=value");
    field = buf + 4;                        /* points at "value" */

    buf = realloc(buf, 256);
#if !VP_BAD
    field = buf + 4;                        /* the fix: recompute */
#endif
    other = malloc(16);                     /* receives the old block */
    strcpy(other, "owner-data");

    memcpy(field, "XXXX", 4);
    vp_reached();
    if (strcmp(other, "owner-data") != 0) {
        vp_effect("corrupted", "the old pointer wrote into another block");
        vp_show("other", other, 10);
    } else
        vp_effect("none", "the write went to the live buffer");
    free(other);
    free(blocker);
    free(buf);
    return vp_end();
}
