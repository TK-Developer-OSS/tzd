/* Managed C comparator handed to the host libc's qsort: called through an
   ABI thunk (raw element pointers -> managed; task #22).  */
#include <stdlib.h>
#include <string.h>

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int main(void)
{
    int v[5] = {5, 3, 4, 1, 2};
    char *s[3];
    int i;
    s[0] = "pear"; s[1] = "apple"; s[2] = "fig";
    qsort(v, 5, sizeof v[0], cmp_int);
    qsort(s, 3, sizeof s[0], cmp_str);   /* elements are managed pointers */
    for (i = 0; i < 5; i++)
        if (v[i] != i + 1)
            return 1;
    return strcmp(s[0], "apple") || strcmp(s[1], "fig") || strcmp(s[2], "pear");
}
