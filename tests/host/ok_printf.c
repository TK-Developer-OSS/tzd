/* The printf family from host headers goes to libc2d's Managed C
   formatter (task #43).  Results are compared with what glibc prints.  */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int vcheck(const char *want, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (strcmp(buf, want) != 0 || n != (int) strlen(want)) {
        printf("mismatch: [%s] want [%s]\n", buf, want);
        return 1;
    }
    return 0;
}

int main(void)
{
    char b[16];
    int bad = 0;
    long big = -9223372036854775807L - 1;
    bad |= vcheck("x=  42|-7   |+5|0x1f|0X1F|017|00042", "x=%4d|%-5d|%+d|%#x|%#X|%#o|%.5d",
                  42, -7, 5, 31, 31, 15, 42);
    bad |= vcheck("[abc][  ab][ab  ][(null)]", "[%s][%4.2s][%-4.2s][%s]", "abc", "abc", "abc", (char *) 0);
    bad |= vcheck("-9223372036854775808 18446744073709551615 255 65535",
                  "%ld %lu %hhu %hu", big, (unsigned long) -1, 511, 131071);
    bad |= vcheck("3.142 1.500000e+00 0.25 % c", "%.3f %e %g %% %c", 3.14159, 1.5, 0.25, 'c');
    bad |= vcheck("|   7|7   |0007|", "|%*d|%-*d|%0*d|", 4, 7, 4, 7, 4, 7);
    bad |= vcheck("(nil)", "%p", (void *) 0);
    /* snprintf truncates and still returns the full length.  */
    if (snprintf(b, 4, "%s", "abcdef") != 6 || strcmp(b, "abc") != 0)
        bad = 1;
    if (sprintf(b, "%05d", 12) != 5 || strcmp(b, "00012") != 0)
        bad = 1;
    fprintf(stdout, "fprintf %s %d\n", "ok", 1);
    printf("printf %s\n", "ok");
    return bad;
}
