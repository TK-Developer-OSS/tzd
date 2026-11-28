/* <string.h>, Managed C.  Written from the C standard's definitions of
   the functions (no code from another library).  Every access is checked by cc1tzd: a call whose length
   reaches past an object traps at the first byte outside it.

   Functions that return a pointer into an argument return it with that
   argument's bounds; "not found" is the null pointer.  */
#include <c2d_rt.h>

int tzd_tolower(int c);

size_t tzd_strlen(const char *s)
{
    size_t n = 0;
    while (s[n] != 0)
        n++;
    return n;
}

/* memcpy.  Pointers inside the bytes keep their bounds, and heap bytes
   keep whether they were written (task #8): __tzd_copy checks both whole
   ranges first (no partial write), then moves bytes, bounds entries and
   written bits together.  */
void *tzd_memcpy(void *dst, const void *src, size_t n)
{
    __tzd_copy(dst, src, n);
    return dst;
}

char *tzd_strcpy(char *dst, const char *src)
{
    tzd_memcpy(dst, src, tzd_strlen(src) + 1);
    return dst;
}

/* memchr.  The N bytes are checked through a narrowed pointer, but the
   result points into S's object (code goes on reading past the searched
   range inside the object: PHP 3's php3_mime_split).  */
void *tzd_memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    if (n)
        p = __tzd_narrow(p, n);
    for (size_t i = 0; i < n; i++)
        if (p[i] == (unsigned char) c)
            return (void *) ((const unsigned char *) s + i);
    return 0;
}

/* memrchr (GNU): the last C in the N bytes at S.  Same checks as memchr.  */
void *tzd_memrchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    if (n)
        p = __tzd_narrow(p, n);
    for (size_t i = n; i > 0; i--)
        if (p[i - 1] == (unsigned char) c)
            return (void *) ((const unsigned char *) s + (i - 1));
    return 0;
}

/* rawmemchr (GNU): the first C at or after S, which must exist inside
   S's object: running off the end is the bounds trap.  */
void *tzd_rawmemchr(const void *s, int c)
{
    const unsigned char *p = s;
    while (*p != (unsigned char) c)
        p++;
    return (void *) p;
}

/* memcmp */
int tzd_memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    if (n) {
        x = __tzd_narrow(x, n);
        y = __tzd_narrow(y, n);
    }
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}

/* memmove: the copy direction handles overlap */
void *tzd_memmove(void *dst, const void *src, size_t n)
{
    __tzd_copy(dst, src, n);    /* overlap is handled like memmove */
    return dst;
}

/* memset.  No pointer survives memset.  */
void *tzd_memset(void *dst, int c, size_t n)
{
    unsigned char *d = dst;
    if (n)
        d = __tzd_narrow(d, n);
    for (size_t i = 0; i < n; i++)
        d[i] = (unsigned char) c;
    tzd_sh_clear(dst, n);
    return dst;
}

/* strchr: the terminating NUL counts as part of the string */
char *tzd_strchr(const char *s, int c)
{
    for (size_t i = 0;; i++) {
        unsigned char x = s[i];
        if (x == (unsigned char) c)
            return (char *) (s + i);
        if (x == 0)
            return 0;
    }
}

/* strrchr */
char *tzd_strrchr(const char *s, int c)
{
    const char *found = 0;
    for (size_t i = 0;; i++) {
        unsigned char x = s[i];
        if (x == (unsigned char) c)
            found = s + i;
        if (x == 0)
            return (char *) found;
    }
}

/* strncmp (strcmp is the unbounded case) */
int tzd_strncmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char x = a[i], y = b[i];
        if (x != y)
            return x - y;
        if (x == 0)
            return 0;
    }
    return 0;
}

int tzd_strcmp(const char *a, const char *b)
{
    return tzd_strncmp(a, b, (size_t) -1);
}

/* strstr */
char *tzd_strstr(const char *s, const char *sub)
{
    size_t len = tzd_strlen(sub);
    /* Try every start; the match must fit before s's NUL.  */
    for (size_t i = 0;; i++) {
        size_t k = 0;
        while (k < len && s[i + k] == sub[k])
            k++;
        if (k == len)
            return (char *) (s + i);
        if (s[i] == 0)
            return 0;
    }
}

/* strcasecmp / strncasecmp (C locale) */
int tzd_strncasecmp(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int x = tzd_tolower((unsigned char) a[i]);
        int y = tzd_tolower((unsigned char) b[i]);
        if (x != y)
            return x - y;
        if (x == 0)
            return 0;
    }
    return 0;
}

int tzd_strcasecmp(const char *a, const char *b)
{
    return tzd_strncasecmp(a, b, (size_t) -1);
}

char *tzd_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    for (; i < n; i++) {
        dst[i] = src[i];
        if (src[i] == 0)
            break;
    }
    for (i++; i < n; i++)
        dst[i] = 0;
    return dst;
}

char *tzd_strncat(char *dst, const char *src, size_t n)
{
    size_t end = tzd_strlen(dst), i;
    for (i = 0; i < n; i++) {
        if (src[i] == 0)
            break;
        dst[end + i] = src[i];
    }
    dst[end + i] = 0;
    return dst;
}

char *tzd_strcat(char *dst, const char *src)
{
    tzd_strcpy(dst + tzd_strlen(dst), src);
    return dst;
}

/* <wchar.h> string and memory functions (written for libc2d, Managed C):
   the host's are not checked, so a call such as wcscpy into a buffer
   sized for a narrow string overflowed inside the host (Juliet CWE135).
   Each checks the whole destination range first (no partial write).  */
typedef __WCHAR_TYPE__ tzd_wchar;

size_t tzd_wcslen(const tzd_wchar *s)
{
    size_t n = 0;
    while (s[n] != 0)
        n++;
    return n;
}

tzd_wchar *tzd_wmemcpy(tzd_wchar *dst, const tzd_wchar *src, size_t n)
{
    tzd_memcpy(dst, src, n * sizeof (tzd_wchar));
    return dst;
}

tzd_wchar *tzd_wmemmove(tzd_wchar *dst, const tzd_wchar *src, size_t n)
{
    tzd_memmove(dst, src, n * sizeof (tzd_wchar));
    return dst;
}

tzd_wchar *tzd_wmemset(tzd_wchar *dst, tzd_wchar c, size_t n)
{
    tzd_wchar *d = dst;
    if (n)
        d = __tzd_narrow(d, n * sizeof (tzd_wchar));
    for (size_t i = 0; i < n; i++)
        d[i] = c;
    return dst;
}

tzd_wchar *tzd_wcscpy(tzd_wchar *dst, const tzd_wchar *src)
{
    tzd_memcpy(dst, src, (tzd_wcslen(src) + 1) * sizeof (tzd_wchar));
    return dst;
}

tzd_wchar *tzd_wcsncpy(tzd_wchar *dst, const tzd_wchar *src, size_t n)
{
    tzd_wchar *d = dst;
    size_t i = 0;
    if (n)
        d = __tzd_narrow(d, n * sizeof (tzd_wchar));
    for (; i < n && src[i] != 0; i++)
        d[i] = src[i];
    for (; i < n; i++)
        d[i] = 0;
    return dst;
}

tzd_wchar *tzd_wcscat(tzd_wchar *dst, const tzd_wchar *src)
{
    tzd_wcscpy(dst + tzd_wcslen(dst), src);
    return dst;
}

tzd_wchar *tzd_wcsncat(tzd_wchar *dst, const tzd_wchar *src, size_t n)
{
    tzd_wchar *d = dst + tzd_wcslen(dst);
    size_t i = 0;
    for (; i < n && src[i] != 0; i++)
        d[i] = src[i];
    d[i] = 0;
    return dst;
}

/* GNU and POSIX string functions busybox uses (written for libc2d,
   Managed C): the host's read and write without bounds.  */
void *tzd_mempcpy(void *dst, const void *src, size_t n)
{
    tzd_memcpy(dst, src, n);
    return (char *) dst + n;
}

char *tzd_stpcpy(char *dst, const char *src)
{
    size_t n = tzd_strlen(src);
    tzd_memcpy(dst, src, n + 1);
    return dst + n;
}

char *tzd_stpncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    size_t i = 0;
    if (n)
        d = __tzd_narrow(d, n);
    for (; i < n && src[i] != 0; i++)
        d[i] = src[i];
    char *end = dst + i;
    for (; i < n; i++)
        d[i] = 0;
    return end;
}

char *tzd_strchrnul(const char *s, int c)
{
    char *p = (char *) s;
    while (*p && *p != (char) c)
        p++;
    return p;
}

char *tzd_strpbrk(const char *s, const char *accept)
{
    for (char *p = (char *) s; *p; p++)
        for (const char *a = accept; *a; a++)
            if (*p == *a)
                return p;
    return 0;
}

char *tzd_strtok_r(char *s, const char *delim, char **save)
{
    char *p = s ? s : *save;
    if (!p)
        return 0;
    for (;;) {                  /* skip leading delimiters */
        const char *d = delim;
        while (*d && *d != *p)
            d++;
        if (!*d || !*p)
            break;
        p++;
    }
    if (!*p) {
        *save = 0;
        return 0;
    }
    char *tok = p;
    for (; *p; p++) {
        const char *d = delim;
        while (*d && *d != *p)
            d++;
        if (*d) {
            *p = 0;
            *save = p + 1;
            return tok;
        }
    }
    *save = 0;
    return tok;
}

/* strtok: tzd_strtok_r with one saved position (a managed pointer, so it
   keeps the bounds of the string being split).  */
char *tzd_strtok(char *s, const char *delim)
{
    static char *save;
    return tzd_strtok_r(s, delim, &save);
}

char *tzd_strcasestr(const char *hay, const char *needle)
{
    size_t n = tzd_strlen(needle);
    for (char *p = (char *) hay; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && tzd_tolower((unsigned char) p[i])
                                == tzd_tolower((unsigned char) needle[i]))
            i++;
        if (i == n)
            return p;
    }
    return n == 0 ? (char *) hay : 0;
}

char *tzd_strsep(char **sp, const char *delim)
{
    char *s = *sp;
    if (!s)
        return 0;
    for (char *p = s; *p; p++)
        for (const char *d = delim; *d; d++)
            if (*p == *d) {
                *p = 0;
                *sp = p + 1;
                return s;
            }
    *sp = 0;
    return s;
}
