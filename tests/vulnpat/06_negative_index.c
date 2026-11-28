/* 06  Signed index checked only against the upper end (CWE-129, writing
   below the buffer: CWE-124).

   Shape: "if (idx < N) table[idx] = v;" with idx a signed int from the
   request.  A negative index passes the check and writes before the
   array, onto the member in front of it: here the connection limit.

   GCC (any flags):  max_conn is rewritten, the program continues.
   TzD:              the slots member carries its own bounds; slots[-1]
                     lies outside them and the store traps.  */
#include "vp.h"

#define NSLOTS 8

struct config {
    int max_conn;
    int slots[NSLOTS];
};

static void set_slot(struct config *c, int idx, int v)
{
#if VP_BAD
    if (idx < NSLOTS)
#else
    if (idx >= 0 && idx < NSLOTS)
#endif
        c->slots[idx] = v;
}

int main(void)
{
    struct config c;
    int idx = VP_RT(VP_BAD ? -1 : 3);       /* index from the request */

    vp_begin("06_negative_index");
    memset(&c, 0, sizeof c);
    c.max_conn = 16;
    set_slot(&c, idx, 100000);
    vp_reached();
    if (c.max_conn != 16)
        vp_effect("corrupted", "max_conn rewritten through slots[-1]");
    else
        vp_effect("none", "max_conn intact");
    return vp_end();
}
