#include <stdio.h>

/* Task #1 (b): a pointer in memory is 8 bytes, so sizeof, offsets and
   struct layout are C's.  Bounds travel with stores, struct copies, static
   initializers and pointers whose address is taken.  */
struct node { char tag; char *name; int n; struct node *next; };

static char greet[] = "hello";
static char *names[] = { "alpha", greet, 0 };
static struct node tail = { 't', "tail", 2, 0 };
static struct node head = { 'h', "head", 1, &tail };

static void set(char **out, char *v) { *out = v; }

int main(void)
{
    struct node a, b;
    char *p;
    char **pp = &p;
    int bad = 0;
    if (sizeof(char *) != 8 || sizeof(struct node) != 32) bad |= 1;
    if ((char *)&a.name - (char *)&a != 8 || (char *)&a.next - (char *)&a != 24)
        bad |= 2;
    a.tag = 'a'; a.name = greet; a.n = 5; a.next = &head;
    b = a;                                  /* aggregate copy keeps bounds */
    if (b.name[4] != 'o' || b.next->next->name[3] != 'l') bad |= 4;
    set(pp, names[0]);                      /* store through T ** */
    if (p[4] != 'a' || names[1][0] != 'h' || names[2] != 0) bad |= 8;
    p = 0;
    if (*pp != 0) bad |= 16;
    printf("%d %s %s\n", (int) sizeof(char *), b.next->name, names[1]);
    return bad;
}
