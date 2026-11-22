/* __attribute__((cleanup(fn))): fn (&var) when var leaves its scope --
   fall-through, return, break, goto -- innermost first, only once the
   declaration was reached (the kernel's guard/scoped_guard/__free).  */
static char log_[32];
static int pos;
static void note(char c) { log_[pos++] = c; log_[pos] = 0; }
typedef struct { char *lock; } guard_t;
static void unlock(guard_t *g) { if (g->lock) note(g->lock[0]); }
static guard_t lock(char *l) { guard_t g = { l }; return g; }
static void put_ptr(char **p) { if (*p) note(**p); }
#define guard(l) guard_t __g __attribute__((cleanup(unlock))) = lock(l)
#define scoped_guard(l) \
    for (guard_t __s __attribute__((cleanup(unlock))) = lock(l), *__d = 0; !__d; __d = (void *) 1)

static char A[] = "a", B[] = "b", C[] = "c", D[] = "d", E[] = "e";

static int f(int early)
{
    guard(A);
    {
        guard(B);
        if (early)
            return 1;           /* b then a */
    }                           /* b */
    note('-');
    return 0;                   /* a */
}

int main(void)
{
    f(1);
    if (__builtin_strcmp(log_, "ba") != 0) return 1;
    pos = 0; f(0);
    if (__builtin_strcmp(log_, "b-a") != 0) return 2;
    pos = 0;
    for (int i = 0; i < 3; i++) {
        guard(C);
        if (i == 1)
            break;              /* c, twice in all */
    }
    if (__builtin_strcmp(log_, "cc") != 0) return 3;
    pos = 0;
    scoped_guard(D) note('x');
    if (__builtin_strcmp(log_, "xd") != 0) return 4;
    pos = 0;
    {
        char *p __attribute__((cleanup(put_ptr))) = E;
        goto out;               /* e */
    }
out:
    if (__builtin_strcmp(log_, "e") != 0) return 5;
    return 0;
}
