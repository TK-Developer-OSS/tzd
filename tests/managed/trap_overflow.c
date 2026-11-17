#include <stdio.h>

/* Classic overflow: writes past the end of a stack array.
   Expected: trap (exit 134) at the store, nothing printed.  */
int main(void)
{
    char buf[4];
    char *p = buf;
    int i;
    for (i = 0; i < 8; i++)
        *p++ = 'x';
    puts("not reached");
    return 0;
}
