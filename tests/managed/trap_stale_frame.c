#include <stdio.h>

/* Leaving a block destroys its pointer-holding locals (bytes overwritten
   twice, bounds dropped): a later frame at the same address cannot pick up
   the old pointer with its old bounds through an uninitialized slot.  */
static char secret[16] = "old frame data";

static void writer(void)
{
    char *p = secret;
    char **pp = &p;              /* p lives in memory */
    printf("%c\n", (*pp)[0]);
}

static void reader(void)
{
    char *q;                     /* same stack slot as writer's p */
    char **qq = &q;
    printf("%c\n", (*qq)[0]);    /* trap: no bounds for the wiped bytes */
}

int main(void)
{
    writer();
    reader();
    return 0;
}
