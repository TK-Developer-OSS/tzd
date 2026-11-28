/* Heap bytes count as written by every way of writing them (task #8):
   stores, ++, calloc, memset, memcpy and strcpy, struct assignment, a
   pointer store (null included), realloc keeping the old contents.  And
   copying unwritten bytes is not reading them: a struct with padding, the
   unused tail of a buffer.  */
#include <stdlib.h>
#include <string.h>
struct pad { char c; long l; };        /* 7 bytes of padding */
struct node { struct node *next; int v; };
int main(void)
{
    int *a = malloc(4 * sizeof *a);
    a[0] = 1; a[1] = 0; a[1]++; a[2] = a[0] + a[1]; a[3] = 0;
    int *z = calloc(4, sizeof *z);
    char *m = malloc(40);
    memset(m, 'x', 40);
    char *s = malloc(32);
    strcpy(s, "hello");
    char *t = malloc(64);
    memcpy(t, s, 32);                  /* bytes 6..31 of s never written */
    struct pad *p = malloc(sizeof *p), *q = malloc(sizeof *q);
    p->c = 'c'; p->l = 7;
    *q = *p;                           /* copies the padding too */
    struct node *n = malloc(sizeof *n);
    n->next = 0; n->v = 3;
    char *g = malloc(16);
    memcpy(g, "0123456789abcdef", 16);
    g = realloc(g, 1000);
    int sum = a[0] + a[1] + a[2] + a[3] + z[3] + m[39] + s[4] + t[4]
              + q->c + (int) q->l + (n->next ? 100 : n->v) + g[15];
    free(a); free(z); free(m); free(s); free(t); free(p); free(q);
    free(n); free(g);
    return sum == 1 + 1 + 2 + 0 + 0 + 'x' + 'o' + 'o' + 'c' + 7 + 3 + 'f' ? 0 : 1;
}
