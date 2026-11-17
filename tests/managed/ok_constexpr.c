/* The kernel's __is_constexpr: the type of ?: depends on whether one arm
   is a null pointer constant, an integer constant expression in the C
   sense -- (long) x * 0 folds to 0 but is not one when x is a variable.
   sizeof (void) is 1 (GNU C), *(void *) is allowed inside sizeof.  An
   empty member declaration ";" is accepted (task #42).  */
#define __is_constexpr(x) \
    (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))

struct guard { void *lock; ; };
enum { E = 3 };

int main(void)
{
    int v = 7;
    const int c = 4;
    if (!__is_constexpr(5) || !__is_constexpr(E + 1) || !__is_constexpr(sizeof(v)))
        return 1;
    if (__is_constexpr(v) || __is_constexpr(c) || __is_constexpr(v + 1))
        return 2;
    if (sizeof(void) != 1 || sizeof(struct guard) != 8)
        return 3;
    return 0;
}
