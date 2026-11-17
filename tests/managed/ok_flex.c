#include <stdio.h>

/* Trailing-array idiom (Apache pools/tables use `char data[1]`): the last
   member may reach the end of the enclosing allocation.  */
struct blk {
    int n;
    char data[1];
};

int main(void)
{
    char buf[32];
    struct blk *b = (struct blk *)buf;
    int i;
    b->n = 20;
    for (i = 0; i < b->n; i++)
        b->data[i] = 'a' + i;
    b->data[b->n] = 0;
    printf("%s\n", b->data);
    return 0;
}
