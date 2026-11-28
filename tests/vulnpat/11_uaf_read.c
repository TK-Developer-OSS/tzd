/* 11  Use after free, read through a cached pointer (CWE-416).

   Shape: a "current session" pointer is cached; logout frees the session
   but leaves the cache; the next login allocates its session, and the
   stale cache now reads the new owner's record.
   glibc's per-thread cache hands a freed block back to the next request
   of the same size (last in, first out), so the new session lands exactly
   on the freed one and the stale pointer sees uid 0.

   GCC (any flags):  the stale pointer reads the root session: uid 0, the
                     program continues.
   TzD:              freed memory is wiped and its addresses are never
                     handed out again, so the two sessions cannot alias.
                     The read through the stale pointer is expected to
                     stop (see the README for how it stops today).  */
#include "vp.h"

struct session {
    char user[16];
    int uid;
};

int main(void)
{
    struct session *s, *t, *current;
    int uid;

    vp_begin("11_uaf_read");
    s = malloc(sizeof *s);
    strcpy(s->user, "guest");
    s->uid = 1000;
    current = s;                            /* cached */

    free(s);                                /* logout */
#if !VP_BAD
    current = NULL;                         /* the fix: drop the cache */
#endif
    t = malloc(sizeof *t);                  /* next login */
    strcpy(t->user, "root");
    t->uid = 0;

    if (current == NULL)
        return vp_end();
    uid = VP_RT(current->uid);
    vp_reached();
    if (uid == 0)
        vp_effect("confused", "the stale pointer reads the new root session");
    else if (uid == 1000)
        vp_effect("none", "the stale pointer still reads the old data");
    else
        vp_effect("none", "the stale pointer reads wiped memory");
    return vp_end();
}
