#include <stdio.h>

/* The trailing-array idiom is still bounded by the allocation.  */
struct blk {
    int n;
    char data[1];
};

int main(void)
{
    char buf[16];
    struct blk *b = (struct blk *)buf;
    int i;
    for (i = 0; i < 16; i++)      /* data starts at 4: 12 bytes left */
        b->data[i] = 'x';
    printf("not reached\n");
    return 0;
}
