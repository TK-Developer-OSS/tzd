/* sprintf into a buffer that is too small: glibc writes past it; the
   Managed C formatter traps at the first byte past the destination.  */
#include <stdio.h>

int main(void)
{
    char small[8];
    sprintf(small, "%s", "far more than eight bytes");
    return 0;
}
