#include <stdio.h>

/* Null pointer read.  Expected: trap (exit 134) with "null pointer".  */
int main(void)
{
    char *p = NULL;
    return *p;
}
