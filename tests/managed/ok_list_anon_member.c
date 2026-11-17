/* p->m checks the bytes of m also when m is inside an anonymous union
   (the kernel's sk_buff.list): list_for_each_entry_safe over an empty
   list reads pos->list.next of a pos made from the list head.  */
#include <stddef.h>
struct list_head { struct list_head *next, *prev; };
struct skb {
    union {
        struct { struct skb *next, *prev; void *dev; };
        struct list_head list;
    };
    unsigned long data[16];
};
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct list_head head = { &head, &head };
    struct skb *pos, *n;
    int k = 0;
    for (pos = container_of(head.next, struct skb, list),
         n = container_of(pos->list.next, struct skb, list);
         &pos->list != &head;
         pos = n, n = container_of(n->list.next, struct skb, list))
        k++;
    return k;
}
