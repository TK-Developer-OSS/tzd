/* 19  Control case, outside TzD's current checks: a write that is in
   bounds, to live and initialized memory, by code that should not write
   that memory at all.

   Shape: a cache shares its copy of a file's contents by reference ("zero
   copy"); a transform meant to work on a private copy is given the shared
   buffer and changes it in place, so every later reader sees the changed
   contents.  No pointer leaves its object, nothing is freed, nothing is
   uninitialized: bounds, lifetime and initialization checks all pass.
   This is the class of CVE-2026-31431 ("Copy Fail").

   Bounds, lifetime and initialization do not see this; what is missing
   is who may write the cache.  The cache states it in one comment line
   on its declaration (a write group, task #79): its contents are set by
   its initializer and no function is listed as a writer.  Other
   compilers see a comment.

   GCC (any flags):  the cached contents change.
   TzD:              to_upper is not a writer of the group: its store
                     traps.  The fixed version writes its own copy.  */
#include "vp.h"

struct page {
    char data[32];                          /* the cache's copy */
};

// @tzd group=filecache
static struct page cached = { "#!/bin/sh\necho hello\n" };

static void to_upper(char *p, int n)
{
    int i;
    for (i = 0; i < n && p[i]; i++)
        if (p[i] >= 'a' && p[i] <= 'z')
            p[i] = (char) (p[i] - 'a' + 'A');
}

int main(void)
{
    char *view = cached.data;               /* zero-copy handout */
    char copy[32];

    vp_begin("19_control_unauthorized_write");
#if VP_BAD
    to_upper(view, VP_RT(16));              /* works on the shared buffer */
#else
    memcpy(copy, view, sizeof copy);        /* the fix: a private copy */
    to_upper(copy, VP_RT(16));
#endif
    vp_reached();
    if (strcmp(cached.data, "#!/bin/sh\necho hello\n") != 0) {
        vp_effect("corrupted", "the cached contents were changed in place");
        vp_show("cache", cached.data, 16);
    } else
        vp_effect("none", "the cached contents are intact");
    (void) copy;
    return vp_end();
}
