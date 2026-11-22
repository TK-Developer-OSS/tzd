/* A block freed twice: the second free traps (CWE-415).  */
#include <stdlib.h>
int main (void)
{
    char *p = malloc (16);
    free (p);
    free (p);
    return 0;
}
