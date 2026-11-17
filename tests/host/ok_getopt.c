/* char ** to the host: getopt permutes argv, strtol sets endptr
   (libc2d boundary.c tzd_pp_in/tzd_pp_out; task #23).  Varargs to the host
   printf are passed raw.  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char a0[] = "prog", a1[] = "-a", a2[] = "foo", a3[] = "-b", a4[] = "xx",
         a5[] = "bar";
    char *argv[7];
    char *end;
    char out[64];
    int c, got_a = 0;
    argv[0] = a0; argv[1] = a1; argv[2] = a2; argv[3] = a3; argv[4] = a4;
    argv[5] = a5; argv[6] = NULL;
    long v = strtol("123xyz", &end, 10);
    if (v != 123 || strcmp(end, "xyz") != 0)
        return 1;
    while ((c = getopt(6, argv, "ab:")) != -1)
        if (c == 'a')
            got_a = 1;
        else if (c != 'b' || strcmp(optarg, "xx") != 0)
            return 2;
    /* GNU getopt moved the operands to the end. */
    if (!got_a || optind != 4 || strcmp(argv[4], "foo") || strcmp(argv[5], "bar"))
        return 3;
    argv[4][0] = 'F';            /* still bounded: writes inside "foo" */
    snprintf(out, sizeof out, "%s %d %s", argv[4], 7, argv[5]);
    return strcmp(out, "Foo 7 bar") ? 4 : 0;
}
