/* A plain char holding a negative byte passed to isalpha (the classic
   out-of-range index into the host ctype table) traps in libc2d.  */
#include <ctype.h>

int main(void)
{
    char c = (char) 0xe9;       /* negative on x86-64 */
    return isalpha(c) ? 0 : 1;
}
