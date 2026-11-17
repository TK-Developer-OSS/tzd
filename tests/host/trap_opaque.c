/* An opaque host handle (FILE *) has no reachable bytes (task #1).  */
#include <stdio.h>

int main(void)
{
    FILE *f = fopen("/dev/null", "r");
    char *p = (char *) f;
    if (!f) return 1;
    printf("%d\n", p[0]);                          /* trap */
    return 0;
}
