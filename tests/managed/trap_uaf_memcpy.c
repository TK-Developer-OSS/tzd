/* memcpy into a freed heap block traps before writing (task #8).  */
#include <stdlib.h>
#include <string.h>
int main(void)
{
    char *buf = malloc(16);
    char *field = buf + 4;
    memset(buf, 0, 16);
    buf = realloc(buf, 256);        /* the old block is freed */
    memcpy(field, "XXXX", 4);
    free(buf);
    return 0;
}
