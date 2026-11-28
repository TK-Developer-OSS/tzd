/* task #79: the writers of a group write it directly, through pointers,
   with ++ and through library functions; anyone reads it; a copy taken
   by another function is its own memory.  */
#include <string.h>

struct page { char data[32]; int hits; };

// @tzd group=filecache writer=cache_fill,cache_touch
static struct page cache;

static void cache_fill(const char *s)
{
    char *p = cache.data;
    memset(p, 0, sizeof cache.data);
    memcpy(p, s, strlen(s));
    p[0] = s[0];
    cache.hits = 0;
}

static void cache_touch(void)
{
    struct page *pg = &cache;
    pg->hits++;
    cache.hits += 1;
}

static int reader(const char *v)
{
    char copy[32];
    memcpy(copy, v, sizeof copy);
    copy[0] = 'H';
    return copy[0] == 'H' && strcmp(v, "hello") == 0;
}

int main(void)
{
    cache_fill("hello");
    cache_touch();
    if (!reader(cache.data))
        return 1;
    return cache.hits == 2 ? 0 : 2;
}
