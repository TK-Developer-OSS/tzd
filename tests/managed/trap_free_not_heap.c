/* free of memory not from malloc traps (CWE-590).  */
#include <stdlib.h>
int main (void)
{
    char buf[64];
    buf[0] = 1;
    free (buf);
    return buf[0];
}
