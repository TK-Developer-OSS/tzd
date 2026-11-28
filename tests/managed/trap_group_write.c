/* task #79: a grouped object written through a pointer by a function that
   is not one of its writers traps (a shared cache handed out by
   reference, changed in place).  */
#include <string.h>

// @tzd group=filecache writer=cache_fill
static char cache[32];

static void cache_fill(const char *s)
{
    strcpy(cache, s);
}

static void to_upper(char *p)
{
    for (; *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char) (*p - 'a' + 'A');
}

int main(void)
{
    cache_fill("hello");
    to_upper(cache);
    return 0;
}
