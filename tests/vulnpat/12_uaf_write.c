/* 12  Use after free, write through a dangling pointer (CWE-416).

   Shape: a deferred callback keeps a pointer to a request record and runs
   after the record was freed; by then the block belongs to an access
   check for another user, and the callback's store flips its decision.
   As in 11, glibc returns the freed block to the next same-size request.

   GCC (any flags):  the other user's check is rewritten to "allow", the
                     program continues.
   TzD:              the store through the dangling pointer is expected to
                     stop; the freed block is never handed out again in any
                     case (see the README for how it stops today).  */
#include "vp.h"

struct request {
    char path[24];
    int allowed;
};

struct pending {
    struct request *req;                    /* kept by the callback */
};

static void late_callback(struct pending *p)
{
    if (p->req)
        p->req->allowed = 1;                /* "the request was approved" */
}

int main(void)
{
    struct pending cb;
    struct request *r, *other;

    vp_begin("12_uaf_write");
    r = malloc(sizeof *r);
    strcpy(r->path, "/public");
    r->allowed = 0;
    cb.req = r;

    free(r);                                /* request cancelled */
#if !VP_BAD
    cb.req = NULL;                          /* the fix: cancel the callback */
#endif
    other = malloc(sizeof *other);          /* another user's check */
    strcpy(other->path, "/admin");
    other->allowed = 0;                     /* denied */

    late_callback(&cb);
    vp_reached();
    if (other->allowed)
        vp_effect("corrupted", "the other user's denied check now allows");
    else
        vp_effect("none", "the other user's check is intact");
    free(other);
    return vp_end();
}
