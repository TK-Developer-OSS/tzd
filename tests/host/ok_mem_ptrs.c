/* memcpy/memmove/realloc carry the bounds of pointers inside the copied
   bytes; memset clears them (task #1 (b), libc2d shadow.c).  */
#include <stdlib.h>
#include <string.h>

struct rec { int id; char *name; };

int main(void)
{
    char nm[] = "abc";
    struct rec a, b, *v;
    int i;
    a.id = 1; a.name = nm;
    memcpy(&b, &a, sizeof a);
    if (b.name[2] != 'c') return 1;
    v = malloc(4 * sizeof *v);
    for (i = 0; i < 4; i++) { v[i].id = i; v[i].name = nm; }
    memmove(&v[1], &v[0], 3 * sizeof *v);
    if (v[3].name[1] != 'b' || v[3].id != 2) return 2;
    v = realloc(v, 100 * sizeof *v);
    if (v[2].name[0] != 'a') return 3;
    memset(v, 0, sizeof *v);
    if (v[0].name != NULL) return 4;
    free(v);
    return 0;
}
