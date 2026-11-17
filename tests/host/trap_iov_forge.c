/* A pointer stored into a host struct keeps its own bounds: they cannot
   be widened by the length field next to it (task #1).  */
#include <stdio.h>
#include <sys/uio.h>

int main(void)
{
    char small[4] = "abc";
    struct iovec v;
    v.iov_base = small;
    v.iov_len = 4096;                              /* integer: no effect */
    printf("%c\n", ((char *) v.iov_base)[0]);
    printf("%c\n", ((char *) v.iov_base)[100]);    /* trap */
    return 0;
}
