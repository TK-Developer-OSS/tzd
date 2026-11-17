// SPDX-License-Identifier: MIT
/* Host functions that hand back a host malloc block (realpath, tempnam):
   libc2d copies the result into its own block, so free knows it.  In a
   file of their own with strong references: a static link pulls the host
   functions from libc.a only for programs that call them (a weak
   reference would not pull them, and the call would trap as "without a
   host libc"); programs without a libc never link this file.  */
#include <d/stdlib.h>
#include <d/string.h>
#include <d/pointer.h>
#include "sys.h"

extern void free(void *);

/* realpath through libc2d: the host resolves the path into its own block,
   which is copied into RESOLVED (checked: the result must fit its bounds)
   or into a libc2d block (so free knows it), then given back.  */
extern char *realpath(const char *, char *);

tzd_ptr tzd_realpath(tzd_ptr path, tzd_ptr resolved)
{
    char *r;
    tzd_size_t n;
    tzd_ptr q;
    tzd_strlen(path);               /* terminated inside its bounds */
    r = realpath((const char *) path.addr, 0);
    if (!r)
        return tzd_ptr_null();
    n = 0;
    while (r[n])
        n++;
    n++;
    q = resolved.base ? resolved : tzd_malloc(n);
    if (q.base)
        tzd_memcpy(q, tzd_ptr_make(r, n), n);
    free(r);
    return q;
}

/* tempnam through libc2d, like realpath: the host's name is in a host
   malloc block; libc2d copies it into its own, so free knows it.  DIR
   and PFX may be NULL.  */
extern char *tempnam(const char *, const char *);

tzd_ptr tzd_tempnam(tzd_ptr dir, tzd_ptr pfx)
{
    char *r;
    tzd_size_t n;
    tzd_ptr q;
    if (dir.addr)
        tzd_strlen(dir);            /* terminated inside its bounds */
    if (pfx.addr)
        tzd_strlen(pfx);
    r = tempnam((const char *) dir.addr, (const char *) pfx.addr);
    if (!r)
        return tzd_ptr_null();
    n = 0;
    while (r[n])
        n++;
    n++;
    q = tzd_malloc(n);
    if (q.base)
        tzd_memcpy(q, tzd_ptr_make(r, n), n);
    free(r);
    return q;
}

/* canonicalize_file_name (GNU) is realpath (path, NULL).  */
tzd_ptr tzd_canonicalize_file_name(tzd_ptr path)
{
    return tzd_realpath(path, tzd_ptr_null());
}

/* getcwd (NULL, 0) returns a host malloc block; free must know it, so it
   is copied into a block of libc2d's (like realpath).  With a buffer, the
   host fills it within the buffer's bounds.  */
extern char *getcwd(char *, tzd_size_t);

tzd_ptr tzd_getcwd(tzd_ptr buf, tzd_size_t size)
{
    char *r;
    tzd_size_t n;
    tzd_ptr q;
    if (buf.base) {
        tzd_ptr_pointer(buf, size);      /* the buffer holds SIZE bytes */
        r = getcwd((char *) buf.addr, size);
        return r ? buf : tzd_ptr_null();
    }
    r = getcwd(0, size);
    if (!r)
        return tzd_ptr_null();
    n = 0;
    while (r[n])
        n++;
    n++;
    q = tzd_malloc(n);
    if (q.base)
        tzd_memcpy(q, tzd_ptr_make(r, n), n);
    free(r);
    return q;
}
