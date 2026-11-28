/* task #79: a library function writes with its caller's permission:
   memcpy into a grouped object from a function that is not a writer
   traps at the call.  */
#include <string.h>

// @tzd group=filecache writer=cache_fill
static char cache[32];

static void cache_fill(void) { memcpy(cache, "hello", 6); }

static void patch(char *dst) { memcpy(dst, "HELLO", 6); }

int main(void)
{
    cache_fill();
    patch(cache);
    return 0;
}
