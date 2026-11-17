/* A slot freed twice traps (task #66: the live bitmap, outside the arena). */
#include <stdlib.h>
int main(void)
{
    char *p = malloc(40), *q = p;
    free(p);
    free(q);
    return 0;
}
