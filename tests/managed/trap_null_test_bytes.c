#include <string.h>
#include <stdlib.h>

/* As ok_null_test_bytes.c, but the stale pointer is used after the null
   test: it still has no bounds and traps.  */
struct opts { char *a; int n; };

int main(void)
{
    struct opts *o = calloc(1, sizeof *o);
    long stale = 0x7f0012345000L;
    memcpy(o, &stale, sizeof stale);
    if (o->a != NULL)
        return o->a[0];                        /* trap */
    return 0;
}
