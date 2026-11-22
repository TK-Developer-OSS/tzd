/* %n (the format-string write primitive) traps.  */
#include <stdio.h>

int main(void)
{
    int n = 0;
    printf("abc%n\n", &n);
    return n;
}
