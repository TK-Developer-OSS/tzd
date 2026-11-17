/* p->m checks the bytes of m only: list_for_each_entry_safe reads
   pos->purge.next of a pos made from the list head (in bounds as a
   list_head, not as a whole entry).  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct vb { unsigned long a[8]; struct list_head purge; unsigned long b[8]; };
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })
int main(void)
{
	struct list_head purge = { &purge, &purge };
	struct vb *pos, *n; int k = 0;
	for (pos = container_of(purge.next, struct vb, purge), n = container_of(pos->purge.next, struct vb, purge);
	     &pos->purge != &purge; pos = n, n = container_of(n->purge.next, struct vb, purge))
		k++;
	return k;
}
