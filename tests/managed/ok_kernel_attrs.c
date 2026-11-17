/* Attributes passed to GCC's own handlers (section, visibility, weak,
   alias, noipa, ...), dropped hints (no_sanitize, counted_by) and
   __auto_type (task #42, Linux kernel idioms).  */
#define __section(s) __attribute__((__section__(s)))
#define __weak __attribute__((__weak__))
#define max_t(a, b) ({ __auto_type _a = (a); __auto_type _b = (b); _a > _b ? _a : _b; })

static int init_val __section(".data.tzd_init") = 7;
int hidden_fn(void) __attribute__((visibility("hidden"), noipa));
int hidden_fn(void) { return 3; }
int weak_fn(void) __weak;
int weak_fn(void) { return 4; }
int real_fn(void) { return 5; }
int alias_fn(void) __attribute__((alias("real_fn")));
static int counted[4] __attribute__((__no_sanitize_address__));

int main(void)
{
    const char *s = "abc";
    __auto_type p = s;          /* const char * */
    __auto_type n = 10L;        /* long */
    if (sizeof n != sizeof(long) || p[2] != 'c') return 1;
    if (max_t(3, 9) != 9 || max_t(n, 2L) != 10) return 2;
    if (init_val + hidden_fn() + weak_fn() + alias_fn() + counted[0] != 19) return 3;
    return 0;
}
