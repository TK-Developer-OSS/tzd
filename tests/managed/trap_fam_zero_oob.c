/* ... but not past it.  */
#include <stdlib.h>
struct usage { void *a, *b; unsigned long flags[0]; };

int main(void)
{
    struct usage *u = malloc(sizeof(struct usage) + 64);
    unsigned long *b = u->flags;
    b[8] = 1;
    return 0;
}
