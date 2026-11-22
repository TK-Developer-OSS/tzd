/* The pos made from the list head is not an entry: reading it traps.  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct page { unsigned long flags; struct list_head lru; unsigned long x[4]; };
static struct list_head pgd_list = { &pgd_list, &pgd_list };
#define container_of(ptr, type, member) \
    ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct page *pos = container_of(pgd_list.next, struct page, lru);
    return (int)pos->x[0];
}
