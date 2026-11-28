/* task #79 spec 7: the forms that stay outside kernel mode: an integer
   carried in a void pointer (a thread or callback argument, never read
   through) and an all-ones sentinel.  */
#include <stdint.h>
static int back (void *arg) { return (int) (intptr_t) arg; }
int main (void)
{
    void *tok = (void *) (intptr_t) 7;
    char *fail = (char *) -1;
    if (back (tok) != 7)
        return 1;
    if ((intptr_t) fail != -1)
        return 2;
    return 0;
}
