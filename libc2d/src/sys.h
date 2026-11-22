#ifndef D_SYS_H
#define D_SYS_H
#include <d/pointer.h>
long tzd_sys_write(int fd, tzd_ptr buf, tzd_size_t n);
void tzd_sys_exit(int status) __attribute__((noreturn));
/* Anonymous private RW mapping; returns 0 on failure.  */
void *tzd_sys_mmap(tzd_size_t len);
/* Same, without reserving swap (large sparse tables).  */
void *tzd_sys_mmap_noreserve(tzd_size_t len);
/* Kernel random bytes; 0 on success, -1 on failure.  */
int tzd_sys_getrandom(void *buf, tzd_size_t n);
int tzd_sys_shmstat(int id, void *buf);
int tzd_sys_munmap(void *addr, tzd_size_t len);
/* Anonymous private mapping at ADDR (0: anywhere) with PROT and extra
   FLAGS (MAP_FIXED 0x10, MAP_NORESERVE 0x4000); returns 0 on failure.  */
void *tzd_sys_mmap_at(void *addr, tzd_size_t len, long prot, long flags);
int tzd_sys_mprotect(void *addr, tzd_size_t len, long prot);
int tzd_sys_madvise(void *addr, tzd_size_t len, long advice);
int tzd_sys_mapped(const void *addr, tzd_size_t n);
#endif
