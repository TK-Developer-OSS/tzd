/* libc2d: managed pointer object (hand-lowered form).

   PROVISIONAL REPRESENTATION (task #1 undecided): {addr, base, size}.
   Everything goes through the functions below, so the representation can
   be swapped without touching callers.  The compiler will eventually emit
   these operations for ordinary pointer syntax.

   tzd_ptr_pointer() is the only way to get a native pointer out.  User code
   never calls it; only libc2d leaf code (syscall layer) does.  */
#ifndef D_POINTER_H
#define D_POINTER_H

typedef __SIZE_TYPE__ tzd_size_t;
typedef __PTRDIFF_TYPE__ tzd_ptrdiff_t;

typedef struct tzd_ptr {
    char *addr;      /* current address                         */
    char *base;      /* start of the allocation it derives from */
    tzd_size_t size;   /* size of that allocation in bytes        */
} tzd_ptr;

/* Violation handler (placeholder for exceptions, task #6): prints msg to
   fd 2 and terminates with status 134.  Never returns.  */
void tzd_trap(const char *msg) __attribute__((noreturn));
void tzd_trap_bounds(const char *msg, const void *addr, const void *base,
                     tzd_size_t size, tzd_size_t n) __attribute__((noreturn));
void tzd_trap_ptr(const char *msg, const void *addr, const void *base,
                  tzd_size_t size) __attribute__((noreturn));

tzd_ptr tzd_ptr_null(void);
tzd_ptr tzd_ptr_make(void *base, tzd_size_t size);          /* whole allocation */
tzd_ptr tzd_ptr_add(tzd_ptr p, tzd_ptrdiff_t n);              /* not checked here */
tzd_ptrdiff_t tzd_ptr_diff(tzd_ptr a, tzd_ptr b);             /* same base only   */
int   tzd_ptr_valid(tzd_ptr p, tzd_size_t n);               /* n bytes at p ok? */
void  tzd_ptr_check(tzd_ptr p, tzd_size_t n);               /* trap if not      */
unsigned char tzd_ptr_load8(tzd_ptr p);
void  tzd_ptr_store8(tzd_ptr p, unsigned char v);

/* Native pointer to n checked bytes at p.  libd-internal use only.  The
   bytes from p on count as written (code that is not checked gets them).  */
void *tzd_ptr_pointer(tzd_ptr p, tzd_size_t n);
/* The same for n bytes that leave the process (write(2)): they must have
   been written.  */
void *tzd_ptr_pointer_out(tzd_ptr p, tzd_size_t n);

/* The heap map (task #8; libc2d stdlib.c): which 16-byte granules of the
   heap belong to a live block and which were ever written.  */
extern unsigned long tzd_hp_lo, tzd_hp_span;
extern unsigned char *tzd_hp_map;
void  tzd_hp_wrote(const void *p, tzd_size_t n);
void  tzd_hp_read(const void *p, tzd_size_t n);
void *tzd_hp_handout(void *addr, const void *base, tzd_size_t size);
void  tzd_hp_copy(void *dst, const void *src, tzd_size_t n);
int   tzd_hp_live(const void *p);

/* Write groups (task #79; libc2d group.c): FN stores at ADDR, which lies
   in section tzd_grp.  Traps unless FN is a writer of the object there.  */
void  tzd_grp_store(const void *addr, const char *fn);

#endif
