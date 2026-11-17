#include <stdio.h>

/* Task #21: a pointer taken from &s.f covers only that member.  Writing
   past it (here into the next member) must trap; `secret` is never hit.  */
struct conn {
    int len;
    int secret;
};

static void fill(char *dst, int n)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = 0x41;
}

int main(void)
{
    struct conn c;
    c.secret = 1234;
    fill((char *)&c.len, sizeof c.len);       /* ok: exactly the member */
    printf("len filled, secret=%d\n", c.secret);
    fill((char *)&c.len, sizeof c);           /* reaches c.secret: trap */
    printf("secret=%d\n", c.secret);
    return 0;
}
