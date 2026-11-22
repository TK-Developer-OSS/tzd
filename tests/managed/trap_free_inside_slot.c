/* free of an address inside a slot traps. */
#include <stdlib.h>
int main(void)
{
    char *p = malloc(40);
    free(p + 8);
    return 0;
}
