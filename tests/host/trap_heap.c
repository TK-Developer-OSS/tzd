/* malloc goes to libc2d (task #14): the pointer carries its allocation
   bounds, so a heap overflow traps.  */
#include <stdlib.h>

int main(void)
{
    char *p = malloc(16);
    int i;
    for (i = 0; i <= 16; i++)     /* one byte too far */
        p[i] = 'x';
    free(p);
    return 0;
}
