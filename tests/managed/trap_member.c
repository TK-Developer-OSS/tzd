#include <stdio.h>

/* Overflow of an array inside a struct into the next member (the shape of
   many real CVEs).  Expected: trap (exit 134); `secret` is never touched.  */
struct conn {
    char user[8];
    int secret;
};

static void copy(char *dst, const char *src)
{
    while ((*dst++ = *src++) != 0)
        ;
}

int main(void)
{
    struct conn c;
    c.secret = 1234;
    copy(c.user, "much-too-long-user-name");
    printf("secret=%d\n", c.secret);
    return 0;
}
