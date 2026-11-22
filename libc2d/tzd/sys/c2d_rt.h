/* libc2d runtime entry points for the parts of libc2d written in Managed C
   (libc2d/tzd/).  This directory is on the include path with -isystem, so
   these are host declarations: raw pointers, called with pointer().

   The runtime itself (bounds table, wipe, trap, allocator core) stays
   hand-lowered C under libc2d/src: it is what the checks are built on.  */
#ifndef C2D_RT_H
#define C2D_RT_H

typedef __SIZE_TYPE__ size_t;

void tzd_trap(const char *msg) __attribute__((noreturn));
void tzd_trap_bounds(const char *msg, const void *addr, const void *base,
                     size_t size, size_t n) __attribute__((noreturn));
/* Bounds entries of the pointer slots in [dst, dst+n) become those of
   [src, src+n) (memcpy/memmove carry pointers with their bounds).  */
void tzd_sh_copy(void *dst, const void *src, size_t n);
/* Drop the bounds entries of [p, p+n) (memset: no pointer survives).  */
void tzd_sh_clear(void *p, size_t n);

/* errno lives in the host libc while it serves stdio and the system
   calls (host rule: one int).  */
int *__errno_location(void);
#define C2D_ERANGE 34
#define C2D_EINVAL 22

/* Host stdio (glibc) for the parts libc2d does not own yet: FILE streams
   and floating-point conversions.  FILE is an opaque handle.  */
typedef struct _IO_FILE FILE;
extern FILE *stdout;
size_t fwrite(const void *p, size_t size, size_t n, FILE *f);
long write(int fd, const void *buf, size_t n);   /* host write(2) */
/* The host's snprintf under its own name: a plain `snprintf` declared in
   a system header is redirected to libc2d's tzd_snprintf, which would
   call itself here.  */
int tzd_host_snprintf(char *buf, size_t n, const char *fmt, ...)
    __asm__("snprintf");

#endif
