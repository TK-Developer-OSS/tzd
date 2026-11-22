/* A pointer whose bytes were overwritten by memset (non-zero) has no
   bounds: loading it traps instead of using the forged address.  */
#include <stdio.h>
#include <string.h>

struct rec { int id; char *name; };

int main(void)
{
    char nm[] = "abc";
    struct rec a;
    a.id = 1; a.name = nm;
    memset(&a, 0x41, sizeof a);
    puts(a.name);                  /* trap */
    return 0;
}
