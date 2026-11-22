/* &pos->member forms an address and reads nothing: list_for_each_entry
   on an empty list makes pos from the list head with container_of (not
   an entry) and only compares &pos->lru with the head.  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct page { unsigned long flags; struct list_head lru; unsigned long x[4]; };
static struct list_head pgd_list = { &pgd_list, &pgd_list };
#define container_of(ptr, type, member) \
    ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    int n = 0;
    struct page *pos;
    for (pos = container_of(pgd_list.next, struct page, lru);
         &pos->lru != &pgd_list;
         pos = container_of(pos->lru.next, struct page, lru))
        n++;
    return n;
}
