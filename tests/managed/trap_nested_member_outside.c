/* ... and a nested member outside the bounds still traps.  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct links { unsigned long a[6]; struct list_head defer_sync; unsigned long b[6]; };
struct dev { unsigned long x[4]; struct links links; unsigned long y[4]; };
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct list_head list = { &list, &list };
    struct dev *pos = container_of(list.next, struct dev, links.defer_sync);
    return (int)pos->links.b[0];
}
