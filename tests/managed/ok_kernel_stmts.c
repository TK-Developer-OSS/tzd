/* __label__ local labels (the kernel's ___wait_var_event uses the same
   label name in every expansion), a statement expression whose last
   statement is an if (its value is void, not the if body's), implicit
   function -> void *, __builtin_has_attribute (task #42).  */
static int hook(int x) { return x; }
static void *addr(void *p) { return p; }
#define wait_until(cond) ({ __label__ out; int r = 0; \
    for (;;) { if (cond) goto out; r++; } out: r; })
#define log_if(c, n) ({ if (c) n++; })
/* Endless loops left only by return (gcd); break still works.  */
static unsigned long gcd(unsigned long a, unsigned long b)
{
    for (;;) {
        if (!b) return a;
        unsigned long t = a % b; a = b; b = t;
    }
}
static int first_even(int x) { while (1) { if (!(x & 1)) return x; x++; } }
static int count_to(int n) { int i = 0; for (;;) { if (i == n) break; i++; } return i; }
struct s { char name[8] __attribute__((nonstring)); int x __attribute__((aligned(8))); };

int main(void)
{
    int i = 0, j = 0, n = 0;
    int a = wait_until(++i == 3);
    int b = wait_until(++j == 5);
    struct s v;
    log_if(a == 2, n);
    log_if(b == 9, n);
    if (a != 2 || b != 4 || n != 1) return 1;
    if (addr(hook) == 0) return 2;
    if (gcd(12, 18) != 6 || first_even(7) != 8 || count_to(4) != 4) return 4;
    if (__builtin_has_attribute(v.name, nonstring) || !__builtin_has_attribute(v.x, aligned))
        return 3;
    return 0;
}
