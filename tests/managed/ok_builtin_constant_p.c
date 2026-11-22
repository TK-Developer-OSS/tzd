/* tzd-flags: -O2 */
/* __builtin_constant_p of a variable is decided after optimization, as
   in C: at -O2 a local that holds a constant is a constant (the kernel's
   statically_true in min()/max() relies on it), and the call of the
   never-defined function below is removed.  At -O0 the answer is 0 and
   the call is not reached either way.  */
extern void not_constant (void);

#define is_nonneg(x) (__builtin_constant_p ((long long)(x) >= 0) \
                      && (long long)(x) >= 0)

int main (void)
{
    __auto_type u = 1000000L;
    long v = 5;
    if (__builtin_constant_p (v) != __builtin_constant_p (u))
        return 1;
#ifdef __OPTIMIZE__
    if (!is_nonneg (u) || !__builtin_constant_p (v))
        not_constant ();
#endif
    return __builtin_constant_p (3) ? 0 : 1;
}
