/* The searched range itself must be inside the object.  */
#include <string.h>
int main (void)
{
    char buf[8] = "abcdefg";
    volatile size_t n = 9;
    return memchr (buf + 1, 'z', n) != 0;
}
