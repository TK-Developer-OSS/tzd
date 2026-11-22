#include <stdio.h>

/* Task #29: bounds cannot be forged by writing a pointer's bytes.  The
   8 bytes of p are rewritten through an integer pointer to aim at secret;
   the next load of p finds a value its bounds were not recorded for.  */
static char buf[8];
static char secret[8] = "s3cr3t";

int main(void)
{
    char *p = buf;
    long *raw = (long *) &p;
    *raw = (long) secret;          /* forged address, no bounds */
    printf("%c\n", p[0]);          /* trap */
    return 0;
}
