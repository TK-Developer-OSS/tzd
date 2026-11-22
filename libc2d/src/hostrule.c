/* Sizes for the bounds rules of pointers coming from the host (cc1tzd
   host_ptr_rules, task #1).  Each returns the number of bytes the managed
   pointer may reach; 0 for a null or failed result.  */
#include <d/pointer.h>
#include "sys.h"

tzd_size_t tzd_host_strsize(const void *p)
{
    const char *s = (const char *) p;
    tzd_size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return n + 1;
}

/* Wide string (wchar_t, 4 bytes on x86-64), including the terminator.  */
tzd_size_t tzd_host_wcssize(const void *p)
{
    const int *s = (const int *) p;
    tzd_size_t n = 0;
    if (!s)
        return 0;
    while (s[n])
        n++;
    return (n + 1) * sizeof(int);
}

/* NULL-terminated vector of pointers, including the terminator.  */
tzd_size_t tzd_host_vecsize(const void *p)
{
    void *const *v = (void *const *) p;
    tzd_size_t n = 0;
    if (!v)
        return 0;
    while (v[n])
        n++;
    return (n + 1) * sizeof(void *);
}

/* mmap: MAP_FAILED gives nothing.  */
tzd_size_t tzd_host_mapsize(const void *p, tzd_size_t len)
{
    if (!p || p == (const void *) -1)
        return 0;
    return len;
}

/* shmat: the segment size (shmctl IPC_STAT); nothing on failure.  */
tzd_size_t tzd_host_shmsize(const void *p, int id)
{
    unsigned char ds[112];      /* struct shmid_ds (x86-64) */
    tzd_size_t sz;
    if (!p || p == (const void *) -1)
        return 0;
    if (tzd_sys_shmstat(id, ds) != 0)
        tzd_trap("libc2d: shmat: cannot read the segment size\n");
    __builtin_memcpy(&sz, ds + 48, sizeof sz);   /* shm_segsz */
    return sz;
}
