/* malloc/realloc/free through libc2d, strdup from the host freed through
   libc2d (foreign pointer back to the host allocator), string functions.  */
#include <stdlib.h>
#include <string.h>

int main(void)
{
    char *p = malloc(8), *q, *h;
    strcpy(p, "abc");
    p = realloc(p, 32);
    strcat(p, "defgh");
    if (strlen(p) != 8 || strcmp(p, "abcdefgh") != 0)
        return 1;
    q = strchr(p, 'd');
    if (q == NULL || q - p != 3 || memcmp(q, "def", 3) != 0)
        return 2;
    memset(p, 'z', 4);
    if (strncmp(p, "zzzzefgh", 8) != 0)
        return 3;
    h = strdup("host");            /* allocated by the host libc */
    if (strcasecmp(h, "HOST") != 0)
        return 4;
    free(h);
    free(p);
    return 0;
}
