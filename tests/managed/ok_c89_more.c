#include <stdio.h>

/* Bit-fields, anonymous struct/union members, K&R definitions.  */
struct flags {
    unsigned a : 3;
    unsigned b : 5;
    int : 0;
    signed c : 4;
    union {
        int i;
        char ch[4];
    };
    struct { short lo, hi; };
};

static int add(x, y, s)
    int x;
    char *s;
{
    return x + y + s[0];
}

int main(void)
{
    struct flags f;
    f.a = 9;            /* truncates to 1 */
    f.b = 31;
    f.c = -3;
    f.i = 0;
    f.ch[0] = 2;
    f.lo = 7; f.hi = 8;
    printf("%d %d %d %d %d %d\n", f.a, f.b, f.c, f.ch[0], f.lo + f.hi, (int)sizeof f);
    printf("%d\n", add(1, 2, "A"));
    return f.a == 1 && f.b == 31 && f.c == -3 && add(1, 2, "A") == 68 ? 0 : 1;
}
