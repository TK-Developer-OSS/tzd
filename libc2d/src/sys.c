// SPDX-License-Identifier: MIT
/* libc2d syscall leaf (x86-64 Linux).  No libc dependency.  */
#include <d/pointer.h>
#include "sys.h"

static long sys3(long n, long a, long b, long c)
{
    long r;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "0"(n), "D"(a), "S"(b), "d"(c)
                      : "rcx", "r11", "memory");
    return r;
}

long tzd_sys_write(int fd, tzd_ptr buf, tzd_size_t n)
{
    return sys3(1, fd, (long) tzd_ptr_pointer(buf, n), (long) n);
}

void tzd_sys_exit(int status)
{
    for (;;)
        sys3(231, status, 0, 0);
}

static void *mmap_flags(tzd_size_t len, long flags)
{
    long r;
    register long r10 __asm__("r10") = flags;
    register long r8  __asm__("r8")  = -1;
    register long r9  __asm__("r9")  = 0;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "0"(9L), "D"(0L), "S"(len), "d"(3L),   /* PROT_READ|WRITE */
                        "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return (r < 0 && r > -4096) ? 0 : (void *) r;
}

/* mmap(addr, len, prot, flags | MAP_PRIVATE | MAP_ANONYMOUS); 0 on failure.  */
void *tzd_sys_mmap_at(void *addr, tzd_size_t len, long prot, long flags)
{
    long r;
    register long r10 __asm__("r10") = flags | 0x22;
    register long r8  __asm__("r8")  = -1;
    register long r9  __asm__("r9")  = 0;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "0"(9L), "D"(addr), "S"(len), "d"(prot),
                        "r"(r10), "r"(r8), "r"(r9)
                      : "rcx", "r11", "memory");
    return (r < 0 && r > -4096) ? 0 : (void *) r;
}

int tzd_sys_madvise(void *addr, tzd_size_t len, long advice)
{
    return (int) sys3(28, (long) addr, (long) len, advice);
}

int tzd_sys_mprotect(void *addr, tzd_size_t len, long prot)
{
    return (int) sys3(10, (long) addr, (long) len, prot);
}

void *tzd_sys_mmap(tzd_size_t len)
{
    return mmap_flags(len, 0x22);            /* MAP_PRIVATE|MAP_ANONYMOUS */
}

void *tzd_sys_mmap_noreserve(tzd_size_t len)
{
    return mmap_flags(len, 0x4022);          /* ... |MAP_NORESERVE */
}

int tzd_sys_munmap(void *addr, tzd_size_t len)
{
    return (int) sys3(11, (long) addr, (long) len, 0);
}

/* Whether [addr, addr + n) is mapped (mincore(2) on its pages): the trap
   dump reads an object's bytes only then (stale bounds can name memory
   that is gone).  */
int tzd_sys_mapped(const void *addr, tzd_size_t n)
{
    unsigned char vec[16];
    unsigned long a = (unsigned long) addr & ~4095UL;
    unsigned long e = (unsigned long) addr + (n ? n : 1);
    for (; a < e; a += 4096UL * 16) {
        unsigned long len = e - a < 4096UL * 16 ? e - a : 4096UL * 16;
        if (sys3(27, (long) a, (long) len, (long) vec) != 0)
            return 0;
    }
    return 1;
}

/* getrandom(2): fills buf completely or returns -1.  */
int tzd_sys_getrandom(void *buf, tzd_size_t n)
{
    char *b = (char *) buf;
    while (n > 0) {
        long r = sys3(318, (long) b, (long) n, 0);
        if (r == -4)                        /* EINTR */
            continue;
        if (r <= 0)
            return -1;
        b += r;
        n -= (tzd_size_t) r;
    }
    return 0;
}

/* shmctl(id, IPC_STAT, buf); 0 on success.  */
int tzd_sys_shmstat(int id, void *buf)
{
    return sys3(31, id, 2, (long) buf) == 0 ? 0 : -1;
}
