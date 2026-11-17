#include <stdlib.h>

/* &elm->next.tqe_next reaches the TAILQ link pair only, not the members
   around it (ok_tailq_last.c is the allowed use).  */
#define TAILQ_ENTRY(type) struct { struct type *tqe_next; struct type **tqe_prev; }
struct item { int v; TAILQ_ENTRY(item) next; char tag[4]; };

int main(void)
{
    struct item *a = calloc(1, sizeof *a);
    char *p = (char *) &a->next.tqe_next;
    p[16] = 'x';                    /* tag[0]: outside the link pair */
    return 0;
}
