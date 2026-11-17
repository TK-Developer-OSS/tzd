/* A trailing [0] array (the old flexible-array spelling, the kernel's
   mem_section_usage.pageblock_flags[0]) reaches the end of the
   allocation, like [].  */
#include <stdlib.h>
struct usage { void *a, *b; unsigned long flags[0]; };

int main(void)
{
    struct usage *u = malloc(sizeof(struct usage) + 64);
    unsigned long *b = u->flags;
    b[7] = 1;
    return (int)b[7] - 1;
}
