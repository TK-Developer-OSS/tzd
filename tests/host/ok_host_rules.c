/* Pointers from the host get bounds from rules (task #1): strings to their
   NUL, objects to their type, fields of host structs by field rules, and
   pointers user code stores into host structs keep the bounds they had.  */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/uio.h>

extern int tab[];                /* size from the definition below */

int main(void)
{
    char buf[16] = "hello";
    struct iovec v[2];
    struct passwd *pw;
    struct tm *tm;
    time_t t = 0;
    char *e;
    setenv("TZD_RULE", "abc", 1);
    e = getenv("TZD_RULE");
    if (!e || e[3] != 0) return 1;                 /* [e, e+4) */
    tm = gmtime(&t);
    if (!tm || tm->tm_year != 70) return 2;
    pw = getpwuid(getuid());
    if (pw && pw->pw_name[0] == 0) return 3;
    v[0].iov_base = buf;                           /* recorded */
    v[0].iov_len = 5;
    if (((char *) v[0].iov_base)[15] != 0) return 4;   /* buf's bounds */
    if (tab[4] != 5) return 5;
    return 0;
}

int tab[5] = { 1, 2, 3, 4, 5 };
