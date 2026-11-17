/* Structs holding pointers passed and returned by value (TK 2026-11-09,
   (b); final rule #46): lowered to hidden pointers with bounds copies, so
   the pointers keep their bounds.  The kernel's cleanup.h guard
   constructors return such a struct.  */
#include <string.h>

typedef struct { char *lock; int n; } guard_t;
struct big { char *p; char *q; long x[4]; };

static guard_t guard_make(char *l) { guard_t g = { l, 1 }; return g; }
static guard_t guard_lit(char *l) { return (guard_t){ .lock = l, .n = 2 }; }
static char guard_peek(guard_t g, int i) { return g.lock[i]; }
struct big big_make(char *p) { struct big b = { p, p + 1, { 1, 2, 3, 4 } }; return b; }
long big_sum(struct big b) { return b.x[0] + b.x[3] + (b.q - b.p); }

int main(void)
{
    char buf[4] = "abc";
    guard_t g = guard_make(buf);
    guard_t (*mk)(char *) = guard_lit;
    guard_t h = mk(buf + 1);
    struct big b = big_make(buf);
    if (g.n != 1 || g.lock[2] != 'c' || h.n != 2 || h.lock[0] != 'b') return 1;
    if (guard_peek(g, 1) != 'b' || guard_peek(guard_make(buf), 0) != 'a') return 2;
    if (big_sum(b) != 6 || b.q[0] != 'b' || big_sum(big_make(buf)) != 6) return 3;
    g = guard_lit(buf + 2);
    if (g.lock[0] != 'c' || strlen(g.lock) != 1) return 4;
    return 0;
}
