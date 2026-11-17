/* p->arr decays to an address and reads nothing: fib_trie's loop ends
   with container_of(NULL, struct tnode, rcu)->kv.  */
#include <stddef.h>
struct rcu { struct rcu *next; };
struct kv { unsigned key; };
struct tnode { struct rcu rcu; struct kv kv[1]; };
#define container_of(ptr, type, member) ({ void *__mptr = (void *)(ptr); ((type *)(__mptr - offsetof(type, member))); })

int main(void)
{
    struct tnode t = { { 0 }, { { 7 } } };
    struct rcu *head = &t.rcu;
    struct kv *kv = t.kv;
    int n = 0;
    while (head) {
        head = head->next;
        n += kv->key;
        kv = container_of(head, struct tnode, rcu)->kv;
    }
    return n == 7 ? 0 : 1;
}
