#include <stdio.h>

/* Managed C: pointers passed to functions, indexing, string literals,
   struct members, pointer difference.  Expected: exit 0.  */
struct rec {
    int id;
    char name[8];
};

static int length(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

static void fill(char *dst, int n, char c)
{
    int i;
    for (i = 0; i < n; i++)
        dst[i] = c;
    dst[n] = 0;
}

int main(void)
{
    char buf[8];
    struct rec r;
    struct rec *rp = &r;
    const char *msg = "managed";

    fill(buf, 7, 'z');
    if (length(buf) != 7)
        return 1;
    if (length(msg) != 7)
        return 2;
    rp->id = 42;
    fill(rp->name, 3, 'a');
    if (r.id != 42 || length(r.name) != 3)
        return 3;
    printf("%s %d %s\n", msg, rp->id, rp->name);
    return 0;
}
