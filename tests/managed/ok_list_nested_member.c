/* p->a.b checks the bytes of b only: list_for_each_entry_safe over a
   member nested in another (the kernel's dev->links.defer_sync) reads
   pos->links.defer_sync.next of a pos made from the list head.  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct links { unsigned long a[6]; struct list_head defer_sync; unsigned long b[6]; };
struct dev { unsigned long x[4]; struct links links; unsigned long y[4]; };
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct list_head list = { &list, &list };
    struct dev *pos, *n;
    int k = 0;
    for (pos = container_of(list.next, struct dev, links.defer_sync),
         n = container_of(pos->links.defer_sync.next, struct dev, links.defer_sync);
         &pos->links.defer_sync != &list;
         pos = n, n = container_of(n->links.defer_sync.next, struct dev, links.defer_sync))
        k++;
    return k;
}
