/* __builtin_mul_overflow with a managed result pointer (checked, then
   raw), and a compound literal inside a static object's initializer (the
   kernel's DEFINE_RATELIMIT_STATE / __RAW_SPIN_LOCK_UNLOCKED): task #42.  */
typedef struct { int raw; const char *name; } spin_t;
struct rs { spin_t lock; int burst; };
#define SPIN_INIT(n) (spin_t){ .raw = 0, .name = #n }

static int warn(void)
{
    static struct rs state = { .lock = SPIN_INIT(state), .burst = 10 };
    return state.burst + state.lock.name[0];
}

int main(void)
{
    unsigned long r;
    int small;
    if (__builtin_mul_overflow(6UL, 7UL, &r) || r != 42) return 1;
    if (!__builtin_mul_overflow(1 << 30, 4, &small)) return 2;
    if (warn() != 10 + 's') return 3;
    return 0;
}
