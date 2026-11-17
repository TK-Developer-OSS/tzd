// SPDX-License-Identifier: MIT
/* printf family, Managed C (written for libc2d, task #43).

   One formatting engine (fmt_core) writes into a managed buffer, so
   sprintf/vsprintf stop with a trap at the first byte past the
   destination instead of overwriting what follows it.  snprintf keeps
   ISO C truncation.  The FILE functions format into a buffer and hand the
   bytes to the host stream (FILE stays in glibc for now).

   Not taken from glibc: %n traps (a format string must not write memory;
   it is the classic format-string attack), and an unknown conversion
   traps instead of being printed.  Floating-point conversions
   (%e %f %g %a) are still done by the host snprintf on a local buffer.

   Varargs pointers are trusted as passed (task #45, deferred by TK).  */
#include <c2d_rt.h>

void *tzd_malloc(size_t n);
void tzd_free(void *p);
size_t tzd_strlen(const char *s);

typedef __builtin_va_list va_list;

struct sink {
    char *buf;          /* null: count only */
    size_t cap;         /* bytes that may be written, including the NUL */
    size_t len;         /* bytes produced so far */
};

static void put(struct sink *o, char c)
{
    if (o->buf && o->len + 1 < o->cap)
        o->buf[o->len] = c;
    o->len++;
}

static void put_n(struct sink *o, char c, int n)
{
    while (n-- > 0)
        put(o, c);
}

enum { F_MINUS = 1, F_PLUS = 2, F_SPACE = 4, F_HASH = 8, F_ZERO = 16 };

static void put_int(struct sink *o, unsigned long long v, int neg, int base,
                    int upper, int flags, int width, int prec)
{
    char digits[24];
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int nd = 0, npre = 0, zeros, pad;
    char pre[2];
    while (v) {
        digits[nd++] = set[v % (unsigned) base];
        v /= (unsigned) base;
    }
    if (prec < 0)
        prec = 1;
    else
        flags &= ~F_ZERO;
    zeros = prec > nd ? prec - nd : 0;
    if (base == 8 && (flags & F_HASH) && zeros == 0
        && (nd == 0 || digits[nd - 1] != '0'))
        zeros = 1;
    if (neg)
        pre[npre++] = '-';
    else if (flags & F_PLUS)
        pre[npre++] = '+';
    else if (flags & F_SPACE)
        pre[npre++] = ' ';
    if (base == 16 && (flags & F_HASH) && nd > 0) {
        pre[npre++] = '0';
        pre[npre++] = upper ? 'X' : 'x';
    }
    pad = width - npre - zeros - nd;
    if (!(flags & (F_MINUS | F_ZERO)))
        put_n(o, ' ', pad);
    for (int i = 0; i < npre; i++)
        put(o, pre[i]);
    if (flags & F_ZERO)
        put_n(o, '0', pad);
    put_n(o, '0', zeros);
    while (nd > 0)
        put(o, digits[--nd]);
    if (flags & F_MINUS)
        put_n(o, ' ', pad);
}

static void put_str(struct sink *o, const char *s, int flags, int width,
                    int prec)
{
    size_t n = 0;
    /* With a precision only that many bytes are read (no NUL needed).  */
    while ((prec < 0 || n < (size_t) prec) && s[n])
        n++;
    int pad = width > (int) n ? width - (int) n : 0;
    if (!(flags & F_MINUS))
        put_n(o, ' ', pad);
    for (size_t i = 0; i < n; i++)
        put(o, s[i]);
    if (flags & F_MINUS)
        put_n(o, ' ', pad);
}

/* One floating-point conversion through the host snprintf.  */
static void put_fp(struct sink *o, char conv, int flags, int width, int prec,
                   int is_long, va_list *ap)
{
    char spec[16], tmp[128];
    int k = 0, n;
    spec[k++] = '%';
    if (flags & F_MINUS) spec[k++] = '-';
    if (flags & F_PLUS)  spec[k++] = '+';
    if (flags & F_SPACE) spec[k++] = ' ';
    if (flags & F_HASH)  spec[k++] = '#';
    if (flags & F_ZERO)  spec[k++] = '0';
    spec[k++] = '*';
    spec[k++] = '.';
    spec[k++] = '*';
    if (is_long)
        spec[k++] = 'L';
    spec[k++] = conv;
    spec[k] = 0;
    if (prec < 0)
        prec = -1;          /* "%.*f" with a negative precision = none */
    if (is_long) {
        long double v = __builtin_va_arg(*ap, long double);
        n = tzd_host_snprintf(tmp, sizeof tmp, spec, width, prec, v);
        if (n >= (int) sizeof tmp) {
            char *big = tzd_malloc((size_t) n + 1);
            if (!big)
                tzd_trap("libc2d: printf out of memory\n");
            tzd_host_snprintf(big, (size_t) n + 1, spec, width, prec, v);
            for (int i = 0; i < n; i++)
                put(o, big[i]);
            tzd_free(big);
            return;
        }
    } else {
        double v = __builtin_va_arg(*ap, double);
        n = tzd_host_snprintf(tmp, sizeof tmp, spec, width, prec, v);
        if (n >= (int) sizeof tmp) {
            char *big = tzd_malloc((size_t) n + 1);
            if (!big)
                tzd_trap("libc2d: printf out of memory\n");
            tzd_host_snprintf(big, (size_t) n + 1, spec, width, prec, v);
            for (int i = 0; i < n; i++)
                put(o, big[i]);
            tzd_free(big);
            return;
        }
    }
    if (n < 0)
        tzd_trap("libc2d: printf floating-point conversion failed\n");
    for (int i = 0; i < n; i++)
        put(o, tmp[i]);
}

static void fmt_core(struct sink *o, const char *fmt, va_list *ap)
{
    for (size_t i = 0; fmt[i]; i++) {
        char c = fmt[i];
        if (c != '%') {
            put(o, c);
            continue;
        }
        int flags = 0, width = 0, prec = -1, len = 0;
        /* flags */
        for (;; i++) {
            char f = fmt[i + 1];
            if (f == '-') flags |= F_MINUS;
            else if (f == '+') flags |= F_PLUS;
            else if (f == ' ') flags |= F_SPACE;
            else if (f == '#') flags |= F_HASH;
            else if (f == '0') flags |= F_ZERO;
            else break;
        }
        i++;
        /* width */
        if (fmt[i] == '*') {
            width = __builtin_va_arg(*ap, int);
            if (width < 0) {
                flags |= F_MINUS;
                width = width == -2147483647 - 1 ? 2147483647 : -width;
            }
            i++;
        } else
            while (fmt[i] >= '0' && fmt[i] <= '9') {
                if (width > 100000000)
                    tzd_trap("libc2d: printf field width too large\n");
                width = width * 10 + (fmt[i++] - '0');
            }
        /* precision */
        if (fmt[i] == '.') {
            i++;
            prec = 0;
            if (fmt[i] == '*') {
                prec = __builtin_va_arg(*ap, int);
                if (prec < 0)
                    prec = -1;
                i++;
            } else
                while (fmt[i] >= '0' && fmt[i] <= '9') {
                    if (prec > 100000000)
                        tzd_trap("libc2d: printf precision too large\n");
                    prec = prec * 10 + (fmt[i++] - '0');
                }
        }
        if (flags & F_MINUS)
            flags &= ~F_ZERO;
        if (flags & F_PLUS)
            flags &= ~F_SPACE;
        /* length: 1 hh, 2 h, 3 l, 4 ll/q/j, 5 z/t, 6 L */
        switch (fmt[i]) {
        case 'h':
            len = fmt[i + 1] == 'h' ? (i++, 1) : 2;
            i++;
            break;
        case 'l':
            len = fmt[i + 1] == 'l' ? (i++, 4) : 3;
            i++;
            break;
        case 'q': case 'j': len = 4; i++; break;
        case 'z': case 't': len = 5; i++; break;
        case 'L': len = 6; i++; break;
        }
        char conv = fmt[i];
        switch (conv) {
        case 'd': case 'i': {
            long long v;
            if (len == 4) v = __builtin_va_arg(*ap, long long);
            else if (len == 3 || len == 5) v = __builtin_va_arg(*ap, long);
            else v = __builtin_va_arg(*ap, int);
            if (len == 1) v = (signed char) v;
            else if (len == 2) v = (short) v;
            unsigned long long m = v < 0 ? 0 - (unsigned long long) v
                                         : (unsigned long long) v;
            put_int(o, m, v < 0, 10, 0, flags, width, prec);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': {
            unsigned long long v;
            if (len == 4) v = __builtin_va_arg(*ap, unsigned long long);
            else if (len == 3 || len == 5) v = __builtin_va_arg(*ap, unsigned long);
            else v = __builtin_va_arg(*ap, unsigned);
            if (len == 1) v = (unsigned char) v;
            else if (len == 2) v = (unsigned short) v;
            flags &= ~(F_PLUS | F_SPACE);
            put_int(o, v, 0, conv == 'u' ? 10 : conv == 'o' ? 8 : 16,
                    conv == 'X', flags, width, prec);
            break;
        }
        case 'c': {
            char ch = (char) __builtin_va_arg(*ap, int);
            int pad = width > 1 ? width - 1 : 0;
            if (!(flags & F_MINUS))
                put_n(o, ' ', pad);
            put(o, ch);
            if (flags & F_MINUS)
                put_n(o, ' ', pad);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(*ap, const char *);
            put_str(o, s ? s : "(null)", flags, width,
                    s ? prec : (prec >= 0 && prec < 6 ? 0 : -1));
            break;
        }
        case 'p': {
            void *p = __builtin_va_arg(*ap, void *);
            if (!p)
                put_str(o, "(nil)", flags, width, -1);
            else
                put_int(o, (unsigned long) p, 0, 16, 0, flags | F_HASH,
                        width, prec);
            break;
        }
        case 'e': case 'E': case 'f': case 'F': case 'g': case 'G':
        case 'a': case 'A':
            put_fp(o, conv, flags, width, prec, len == 6, ap);
            break;
        case '%':
            put(o, '%');
            break;
        case 'n':
            tzd_trap("libc2d: printf %n is not supported (a format must not write memory)\n");
        default:
            tzd_trap("libc2d: printf: unknown conversion in format\n");
        }
    }
}

static int done(struct sink *o)
{
    if (o->buf && o->cap > 0)
        o->buf[o->len < o->cap ? o->len : o->cap - 1] = 0;
    if (o->len > 2147483647)
        tzd_trap("libc2d: printf result longer than INT_MAX\n");
    return (int) o->len;
}

int tzd_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
    struct sink o = { n ? buf : 0, n, 0 };
    va_list aq;
    __builtin_va_copy(aq, ap);
    fmt_core(&o, fmt, &aq);
    __builtin_va_end(aq);
    return done(&o);
}

int tzd_snprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    __builtin_va_start(ap, fmt);
    int r = tzd_vsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

/* sprintf: no length is given, so the destination's own bounds are the
   limit: the first byte past it traps.  */
int tzd_vsprintf(char *buf, const char *fmt, va_list ap)
{
    struct sink o = { buf, (size_t) -1, 0 };
    va_list aq;
    __builtin_va_copy(aq, ap);
    fmt_core(&o, fmt, &aq);
    __builtin_va_end(aq);
    return done(&o);
}

int tzd_sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    __builtin_va_start(ap, fmt);
    int r = tzd_vsprintf(buf, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

/* FILE output: format into a buffer, then the host stream writes it.  */
int tzd_vfprintf(FILE *f, const char *fmt, va_list ap)
{
    char small[512];
    va_list aq;
    __builtin_va_copy(aq, ap);
    int n = tzd_vsnprintf(small, sizeof small, fmt, aq);
    __builtin_va_end(aq);
    if ((size_t) n < sizeof small)
        return fwrite(small, 1, (size_t) n, f) == (size_t) n ? n : -1;
    char *big = tzd_malloc((size_t) n + 1);
    if (!big)
        tzd_trap("libc2d: printf out of memory\n");
    tzd_vsnprintf(big, (size_t) n + 1, fmt, ap);
    size_t w = fwrite(big, 1, (size_t) n, f);
    tzd_free(big);
    return w == (size_t) n ? n : -1;
}

int tzd_fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    __builtin_va_start(ap, fmt);
    int r = tzd_vfprintf(f, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

/* printf/vprintf of the host program (glibc's stdout).  The --nolibc
   printf is tzd_printf in src/stdio.c.  */
int tzd_vprintf_stdio(const char *fmt, va_list ap)
{
    return tzd_vfprintf(stdout, fmt, ap);
}

int tzd_printf_stdio(const char *fmt, ...)
{
    va_list ap;
    __builtin_va_start(ap, fmt);
    int r = tzd_vfprintf(stdout, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

/* asprintf/vasprintf: the string in a libc2d block (free knows it).  */
int tzd_vasprintf(char **out, const char *fmt, va_list ap)
{
    va_list aq;
    __builtin_va_copy(aq, ap);
    int n = tzd_vsnprintf(0, 0, fmt, aq);
    __builtin_va_end(aq);
    char *s = tzd_malloc((size_t) n + 1);
    if (!s) {
        *out = 0;
        return -1;
    }
    tzd_vsnprintf(s, (size_t) n + 1, fmt, ap);
    *out = s;
    return n;
}

int tzd_asprintf(char **out, const char *fmt, ...)
{
    va_list ap;
    __builtin_va_start(ap, fmt);
    int r = tzd_vasprintf(out, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

/* dprintf/vdprintf: format, then write(2) to the descriptor.  */

int tzd_vdprintf(int fd, const char *fmt, va_list ap)
{
    char *s;
    int n = tzd_vasprintf(&s, fmt, ap);
    if (n < 0)
        return -1;
    size_t done_n = 0;
    while (done_n < (size_t) n) {
        long w = write(fd, s + done_n, (size_t) n - done_n);
        if (w <= 0)
            break;
        done_n += (size_t) w;
    }
    tzd_free(s);
    return done_n == (size_t) n ? n : -1;
}

int tzd_dprintf(int fd, const char *fmt, ...)
{
    va_list ap;
    __builtin_va_start(ap, fmt);
    int r = tzd_vdprintf(fd, fmt, ap);
    __builtin_va_end(ap);
    return r;
}
