/* memchr's result points into the whole object, not only the searched
   range: reading on past the range (inside the object) is fine.  */
#include <string.h>
int main (void)
{
    char buf[16] = "ab\"cd efgh";
    char *q = memchr (buf, '"', 4);
    if (!q || q != buf + 2)
        return 1;
    return strstr (q, "efgh") == buf + 6 ? 0 : 2;
}
