// SPDX-License-Identifier: MIT
/* qsort, Managed C (written for libc2d).

   The host libc's qsort swaps elements byte by byte, which leaves the
   bounds entries of pointers inside the elements behind (their next load
   traps, task #1 (b)).  This one moves elements with tzd_memcpy, which
   carries the entries.  Heapsort: O(n log n), no recursion, no extra
   memory beyond one element.  The comparator gets each element bounded to
   that element only.  */
#include <c2d_rt.h>

void *tzd_memcpy(void *dst, const void *src, size_t n);
void *tzd_malloc(size_t n);
void tzd_free(void *p);

typedef int (*tzd_cmp_fn)(const void *, const void *);
typedef int (*tzd_cmp_r_fn)(const void *, const void *, void *);

/* The comparison: plain (qsort) or with a caller argument (qsort_r).  */
struct cmpctx {
    tzd_cmp_fn cmp;
    tzd_cmp_r_fn cmp_r;
    void *arg;
};

static int compare(const struct cmpctx *c, const void *a, const void *b)
{
    return c->cmp_r ? c->cmp_r(a, b, c->arg) : c->cmp(a, b);
}

static char *elt(char *base, size_t i, size_t sz)
{
    return __tzd_narrow(base + i * sz, sz);
}

static void swap(char *base, size_t i, size_t j, size_t sz, char *tmp)
{
    tzd_memcpy(tmp, elt(base, i, sz), sz);
    tzd_memcpy(elt(base, i, sz), elt(base, j, sz), sz);
    tzd_memcpy(elt(base, j, sz), tmp, sz);
}

static void sift(char *base, size_t root, size_t n, size_t sz,
                 const struct cmpctx *cmp, char *tmp)
{
    for (;;) {
        size_t c = 2 * root + 1;
        if (c >= n)
            return;
        if (c + 1 < n && compare(cmp, elt(base, c, sz), elt(base, c + 1, sz)) < 0)
            c++;
        if (compare(cmp, elt(base, root, sz), elt(base, c, sz)) >= 0)
            return;
        swap(base, root, c, sz, tmp);
        root = c;
    }
}

static void sort_with(void *vbase, size_t n, size_t sz, const struct cmpctx *cmp)
{
    char *base = vbase, *m, *tmp;
    size_t i;
    if (n < 2 || sz == 0)
        return;
    if (n > ((size_t) -1) / sz)
        tzd_trap("libc2d: qsort size overflow\n");
    base = __tzd_narrow(base, n * sz);
    if (!cmp->cmp && !cmp->cmp_r)
        tzd_trap("libc2d: qsort without a comparison function\n");
    /* Scratch element with the same 8-byte phase as the array, so pointer
       slots keep their bounds while parked there.  */
    m = tzd_malloc(sz + 8);
    if (!m)
        tzd_trap("libc2d: qsort out of memory\n");
    tmp = __tzd_narrow(m + (((unsigned long) base - (unsigned long) m) & 7), sz);
    for (i = n / 2; i > 0; i--)
        sift(base, i - 1, n, sz, cmp, tmp);
    for (i = n - 1; i > 0; i--) {
        swap(base, 0, i, sz, tmp);
        sift(base, 0, i, sz, cmp, tmp);
    }
    tzd_free(m);
}

void tzd_qsort(void *vbase, size_t n, size_t sz, tzd_cmp_fn cmp)
{
    struct cmpctx c = { cmp, 0, 0 };
    sort_with(vbase, n, sz, &c);
}

/* qsort_r (glibc): the comparator also gets the caller's ARG.  */
void tzd_qsort_r(void *vbase, size_t n, size_t sz, tzd_cmp_r_fn cmp, void *arg)
{
    struct cmpctx c = { 0, cmp, arg };
    sort_with(vbase, n, sz, &c);
}

/* bsearch: each element handed to the comparator bounded to itself; the
   result bounded to the array (as a pointer into it).  */
void *tzd_bsearch(const void *key, const void *base, size_t n, size_t sz,
                  tzd_cmp_fn cmp)
{
    char *b = (char *) base;
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int r = cmp(key, elt(b, mid, sz));
        if (r == 0)
            return b + mid * sz;
        if (r < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return 0;
}
