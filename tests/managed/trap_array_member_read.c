/* ... reading an element through it still traps.  */
#include <stddef.h>
struct rcu { struct rcu *next; };
struct kv { unsigned key; };
struct tnode { struct rcu rcu; struct kv kv[1]; };
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct rcu *head = NULL;
    return (int)container_of(head, struct tnode, rcu)->kv[0].key;
}
