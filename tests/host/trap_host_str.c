/* A string from the host is bounded by its NUL (task #1): reading past it
   traps (before the rules, host pointers were unbounded above).  */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char *e;
    setenv("TZD_RULE", "abc", 1);
    e = getenv("TZD_RULE");
    printf("%c\n", e[0]);
    printf("%d\n", e[10]);                         /* trap */
    return 0;
}
