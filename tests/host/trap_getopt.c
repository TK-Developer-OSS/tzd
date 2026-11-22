/* Bounds survive the round trip through the host's getopt: an operand
   moved by the permutation still traps one past its end.  */
#include <unistd.h>

int main(void)
{
    char a0[] = "prog", a1[] = "foo", a2[] = "-a";
    char *argv[4];
    argv[0] = a0; argv[1] = a1; argv[2] = a2; argv[3] = 0;
    while (getopt(3, argv, "a") != -1)
        ;
    argv[2][4] = 'x';            /* "foo" has 4 bytes: index 4 is out */
    return 0;
}
