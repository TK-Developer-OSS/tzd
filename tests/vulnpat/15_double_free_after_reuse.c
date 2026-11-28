/* 15  Double free after the block was handed out again (CWE-415 ->
   CWE-416).

   Shape: a stale owner frees "its" buffer a second time after the
   allocator already gave that block to someone else.  The second free
   releases the other owner's live buffer, and the next allocation of that
   size hands the same block out a second time: two owners, one block.
   At the second free the block is in use, so it is not in glibc's free
   lists and the allocator's own double-free check has nothing to find.

   GCC (any flags):  "config" and "scratch" are the same block; writing the
                     scratch buffer rewrites the config, the program
                     continues.
   TzD:              the first block's addresses are never handed out
                     again, so "config" is a different block; the second
                     free of the first block traps.  */
#include "vp.h"

int main(void)
{
    char *old, *config, *scratch;

    vp_begin("15_double_free_after_reuse");
    old = malloc(32);
    strcpy(old, "request");
    free(old);                              /* first owner done */

    config = malloc(32);                    /* glibc: the same block */
    strcpy(config, "mode=safe");

#if VP_BAD
    free(old);                              /* stale owner frees again */
#endif
    old = NULL;
    scratch = malloc(32);
    strcpy(scratch, "mode=unsafe-scratch");
    vp_reached();
    if (strcmp(config, "mode=safe") != 0) {
        vp_effect("confused", "config and scratch are the same block");
        vp_show("config", config, 19);
    } else
        vp_effect("none", "config is a separate block");
    return vp_end();
}
