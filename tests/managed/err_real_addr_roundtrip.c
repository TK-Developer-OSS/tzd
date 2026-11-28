/* task #79 spec 7: a pointer rebuilt from an integer (here rounded up to
   16 bytes) is a real address; outside kernel mode it is a compile error.  */
#include <stdint.h>
int main (void)
{
    char buf[64];
    char *p = (char *) (((uintptr_t) buf + 15) & ~(uintptr_t) 15);
    *p = 0;
    return 0;
}
