/* typeof, _Generic and __builtin_types_compatible_p (the Linux kernel's
   __unqual_scalar_typeof and __same_type; task #42).  */
#define kind(x) _Generic((x), char: 1, signed char: 2, unsigned char: 3, \
                         int: 4, unsigned int: 5, long: 6, char *: 7, \
                         default: 0)
#define same(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define unqual(x) typeof(_Generic((x), char: (char)0, int: (int)0, default: (x)))

struct s { int a; char *p; };

int main(void)
{
    const int ci = 3;
    char buf[4] = "ab";
    char *p = buf;
    long l = 0;
    typeof(ci) c2 = 5;            /* const int */
    typeof(buf) b2 = "xyz";       /* char[4] */
    typeof(p) p2 = b2;            /* char *, managed */
    __typeof__(struct s) st = { 1, p };
    unqual(ci) w = 7;             /* int, assignable */
    __typeof_unqual__(ci) u = 1;
    w = 8;
    u = 2;
    if (kind(ci) != 4 || kind(buf) != 7 || kind(l) != 6 || kind((char) 1) != 1
        || kind(1u) != 5 || kind(1.0) != 0 || kind((signed char) 1) != 2)
        return 1;
    if (!same(ci, w) || same(l, w) || !same(p, p2)
        || !__builtin_types_compatible_p(int, const int)
        || __builtin_types_compatible_p(int *, const int *)
        || __builtin_types_compatible_p(char, signed char))
        return 2;
    if (sizeof b2 != 4 || p2[1] != 'y' || st.p[0] != 'a' || c2 != 5)
        return 3;
    return w + u - 10;
}
