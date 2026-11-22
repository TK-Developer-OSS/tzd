/* __builtin_choose_expr picks an arm while parsing (the kernel's
   GENMASK_INPUT_CHECK with __is_constexpr); __builtin_constant_p.  */
#define __is_constexpr(x) \
    (sizeof(int) == sizeof(*(8 ? ((void *)((long)(x) * 0l)) : (int *)8)))
#define pick(x) __builtin_choose_expr(__is_constexpr(x), 1, 2)

int main(void)
{
    int v = 3;
    char c = 0;
    if (pick(4) != 1 || pick(v) != 2) return 1;
    if (sizeof(__builtin_choose_expr(1, c, v)) != 1) return 2;
    if (!__builtin_constant_p(10)) return 3;
    return 0;
}
