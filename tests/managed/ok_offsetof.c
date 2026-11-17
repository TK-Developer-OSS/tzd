#include <stdio.h>
#include <stddef.h>

/* offsetof and Apache's XtOffset idiom ((char *)&((T *)NULL)->f - (char *)NULL),
   also in static initializers (command tables).  */
struct rec { int a; char *b; struct { short x, y[3]; } in; };
struct cmd { const char *name; void *info; };

#define XtOffset(p_type,field) \
	((long) (((char *) (&(((p_type)NULL)->field))) - ((char *) NULL)))

static struct cmd tab[] = {
    {"b", (void *)offsetof(struct rec, b)},
    {"y", (void *)XtOffset(struct rec *, in.y)},
    {NULL, NULL}
};

int main(void)
{
    struct rec r;
    long o1 = (long)tab[0].info, o2 = (long)tab[1].info;
    /* Compare with the real member addresses (layout is C's since
       task #1 (b), but the test does not depend on it).  */
    long e1 = (char *)&r.b - (char *)&r, e2 = (char *)&r.in.y - (char *)&r;
    long e3 = (char *)&r.in.y[2] - (char *)&r;
    printf("%d %d %d\n", (int)o1, (int)o2, (int)offsetof(struct rec, in.y[2]));
    return o1 == e1 && o2 == e2 && offsetof(struct rec, in.y[2]) == e3 ? 0 : 1;
}
