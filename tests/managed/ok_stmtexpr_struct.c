/* A statement expression whose value is a struct holding pointers (the
   kernel's conditional lock guards): the value is bounds-copied out
   before the block's locals are wiped (task #42).  */
typedef struct { char *lock; } guard_t;
#define try_guard(l, ok) ({ guard_t _t = { .lock = (l) }, *_T = &_t; \
                            if (!(ok)) _T->lock = 0; _t; })
static guard_t ctor(char *l, int ok) { guard_t t = try_guard(l, ok); return t; }

int main(void)
{
    char buf[3] = "ab";
    guard_t g = try_guard(buf, 1);
    guard_t h = ctor(buf + 1, 1), z = ctor(buf, 0);
    if (g.lock[1] != 'b' || h.lock[0] != 'b' || z.lock) return 1;
    return 0;
}
