/* alloca (N): N bytes, usable inside.  */
#include <string.h>
int main (void)
{
    char *p = __builtin_alloca (8);
    memset (p, 1, 8);
    p[7] = 2;
    return p[0] + p[7] == 3 ? 0 : 1;
}
