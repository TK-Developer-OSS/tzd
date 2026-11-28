/* task #79 spec 7: an integer carried in a void pointer reaches no bytes:
   reading through it, after a cast, traps.  */
#include <stdint.h>
int main (int argc, char **argv)
{
    void *tok = (void *) (intptr_t) (argc + 0x1000);
    int *p = tok;
    (void) argv;
    return *p;
}
