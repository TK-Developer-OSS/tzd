#include <stdlib.h>

/* BSD <sys/queue.h> TAILQ_LAST / TAILQ_PREV (OpenSSH sshconnect2.c
   TAILQ_FOREACH_REVERSE): tqh_last / tqe_prev point at a link's first
   pointer and are cast back to the head type to read its second one.  */
#define TAILQ_HEAD(name, type) struct name { struct type *tqh_first; struct type **tqh_last; }
#define TAILQ_ENTRY(type) struct { struct type *tqe_next; struct type **tqe_prev; }
#define TAILQ_LAST(head, headname) (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_PREV(elm, headname, field) (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))
#define TAILQ_INIT(head) do { (head)->tqh_first = NULL; (head)->tqh_last = &(head)->tqh_first; } while (0)
#define TAILQ_INSERT_TAIL(head, elm, field) do { \
	(elm)->field.tqe_next = NULL; (elm)->field.tqe_prev = (head)->tqh_last; \
	*(head)->tqh_last = (elm); (head)->tqh_last = &(elm)->field.tqe_next; } while (0)

struct item { int v; TAILQ_ENTRY(item) next; char tag[4]; };
TAILQ_HEAD(ilist, item);

int main(void)
{
    struct ilist h;
    struct item *it, *a = calloc(1, sizeof *a), *b = calloc(1, sizeof *b);
    int sum = 0, n = 0;
    TAILQ_INIT(&h);
    if (TAILQ_LAST(&h, ilist) != NULL)
        return 1;
    a->v = 1; b->v = 2;
    TAILQ_INSERT_TAIL(&h, a, next);
    TAILQ_INSERT_TAIL(&h, b, next);
    for (it = TAILQ_LAST(&h, ilist); it != NULL; it = TAILQ_PREV(it, ilist, next))
        sum = sum * 10 + it->v, n++;
    return (n == 2 && sum == 21) ? 0 : 2;
}
