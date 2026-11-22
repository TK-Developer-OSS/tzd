/* ... a member outside the bounds still traps.  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct vb { unsigned long a[8]; struct list_head purge; unsigned long b[8]; };
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })
int main(void)
{
    struct list_head purge = { &purge, &purge };
    struct vb *pos = container_of(purge.next, struct vb, purge);
    return (int)pos->b[0];
}
