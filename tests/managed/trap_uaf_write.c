/* A store through a pointer to a freed heap block traps (task #8).  */
#include <stdlib.h>
struct req { int allowed; };
static struct req *saved;
int main(void)
{
    struct req *r = malloc(sizeof *r);
    r->allowed = 0;
    saved = r;
    free(r);
    saved->allowed = 1;
    return 0;
}
