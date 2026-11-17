#include <stdio.h>

/* Reading through a constant null pointer still traps.  */
struct rec { int a; int b; };

int main(void)
{
    int v = ((struct rec *)0)->b;
    printf("not reached %d\n", v);
    return 0;
}
