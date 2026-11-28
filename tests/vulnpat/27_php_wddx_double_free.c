/* 27  Name freed in one handler and again at cleanup (CWE-415).
   PHP, CVE-2016-5772 (wddx php_wddx_process_data, NVD CVSS 3.1: 9.8).

   Shape: a deserializer keeps the pending variable name on its stack
   entry.  When the value arrives, the handler uses the name and frees
   it but leaves the pointer; at the end the cleanup frees every pending
   name, this one a second time.  Between the two, another string of the
   same size took the block, so the second free releases a live string
   and the next allocation hands that block out twice.

   GCC (any flags):  two live strings share one block; writing the second
                     rewrites the first, the program continues.
   TzD:              the second free of the same block traps (libc2d).  */
#include "vp.h"

struct ent {
    char *varname;
};

static void process_data(struct ent *e)
{
    /* ... the value is stored under e->varname ... */
    free(e->varname);
#if !VP_BAD
    e->varname = NULL;                      /* the fix */
#endif
}

int main(void)
{
    struct ent e;
    char *keep, *next;

    vp_begin("27_php_wddx_double_free");
    e.varname = malloc(32);
    strcpy(e.varname, "username");
    process_data(&e);

    keep = malloc(32);                      /* glibc: the same block */
    strcpy(keep, "role=reader");

    free(e.varname);                        /* cleanup (free(NULL) is a no-op) */
    next = malloc(32);
    strcpy(next, "role=administrator");
    vp_reached();
    if (strcmp(keep, "role=reader") != 0) {
        vp_effect("confused", "two live strings share one block");
        vp_show("keep", keep, 18);
    } else
        vp_effect("none", "the strings are separate");
    return vp_end();
}
