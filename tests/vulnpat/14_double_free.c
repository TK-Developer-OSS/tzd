/* 14  Double free on an error path (CWE-415).

   Shape: the failure branch frees the buffer and jumps to the common
   cleanup, which frees it again.

   GCC:  glibc notices this simple case itself ("free(): double free
         detected in tcache 2") and aborts: exit status 134 *without* a
         "tzd:" message.  The check depends on the allocator's state; 15
         shows a double free that it does not notice.
   TzD:  the second free of a released block traps (exit 134 with a
         "tzd:" message), whatever the allocator's state.  */
#include "vp.h"

static int parse(const char *s)
{
    return s[0] == '{' ? 0 : -1;
}

int main(void)
{
    char *buf;
    int rc = 0;

    vp_begin("14_double_free");
    buf = malloc(32);
    strcpy(buf, VP_RT(0) ? "{}" : "not-json");
    if (parse(buf) < 0) {
        free(buf);
#if !VP_BAD
        buf = NULL;                         /* the fix */
#endif
        rc = -1;
        goto out;
    }
out:
    free(buf);
    vp_reached();
    if (VP_BAD)
        vp_effect("corrupted", "the block was freed twice and nothing noticed");
    else
        vp_effect("none", "freed once");
    (void) rc;
    return vp_end();
}
