#include <stdio.h>

/* Out-of-bounds read through an index.  Expected: trap (exit 134).  */
int main(void)
{
    int a[3];
    int i, sum = 0;
    a[0] = 1; a[1] = 2; a[2] = 3;
    for (i = 0; i <= 3; i++)
        sum += a[i];
    printf("%d\n", sum);
    return 0;
}
