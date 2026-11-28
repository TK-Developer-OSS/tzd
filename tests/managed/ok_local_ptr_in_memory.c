/* Pointers to locals kept in memory while the local lives are fine
   (task #8): in a local struct, in a heap struct cleared before the
   function returns, and through an out parameter of a caller's own
   local.  */
#include <stdlib.h>
struct ctx { char *buf; };
__attribute__((noinline)) static int use(struct ctx *c)
{
    return c->buf[0];
}
__attribute__((noinline)) static void point(char **out, char *where)
{
    *out = where;
}
int main(void)
{
    char local[8] = "abc";
    struct ctx c = { local };
    struct ctx *h = malloc(sizeof *h);
    char *p = 0;
    int r;
    h->buf = local;
    r = use(&c) + use(h);
    h->buf = 0;
    free(h);
    point(&p, local);
    r += p[1];
    return r == 'a' + 'a' + 'b' ? 0 : 1;
}
