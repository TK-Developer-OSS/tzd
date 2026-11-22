#include <stdio.h>

/* Bounds survive an aggregate copy: the copied pointer is still bounded
   to its 4-byte array.  */
struct s { int k; char *p; };

int main(void)
{
    char small[4] = "abc";
    struct s a, b;
    a.k = 1;
    a.p = small;
    b = a;
    printf("%c\n", b.p[2]);
    b.p[10] = 'x';                 /* trap */
    return 0;
}
