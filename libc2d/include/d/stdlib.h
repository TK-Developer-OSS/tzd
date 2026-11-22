#ifndef D_STDLIB_H
#define D_STDLIB_H
#include <d/pointer.h>

/* Allocation returns a pointer object that carries the allocation bounds.
   Placeholder allocator: one mapping per allocation (page granular), so a
   freed block faults on access.  A real allocator comes later (#4, #13).  */
tzd_ptr tzd_aligned_alloc(tzd_size_t alignment, tzd_size_t n);
tzd_ptr tzd_canonicalize_file_name(tzd_ptr path);
tzd_ptr tzd___sched_cpualloc(tzd_size_t n);
void tzd___sched_cpufree(tzd_ptr p);
tzd_ptr tzd_malloc(tzd_size_t n);              /* null tzd_ptr if n==0 or no memory */
tzd_ptr tzd_strdup(tzd_ptr s);                 /* through tzd_malloc */
tzd_ptr tzd_strndup(tzd_ptr s, tzd_size_t max);
tzd_ptr tzd_calloc(tzd_size_t n, tzd_size_t sz); /* zeroed; traps on overflow       */
tzd_ptr tzd_realloc(tzd_ptr p, tzd_size_t n);    /* moves; old bounds end here */
void  tzd_free(tzd_ptr p);                   /* whole allocation only           */

/* Sort n elements of sz bytes at base.  Elements move with the bounds of
   the pointers they hold; cmp sees each element bounded to itself.  */
void  tzd_qsort(tzd_ptr base, tzd_size_t n, tzd_size_t sz,
              int (*cmp)(tzd_ptr, tzd_ptr));

void  tzd_exit(int status) __attribute__((noreturn));
void  tzd_abort(void) __attribute__((noreturn));
int   tzd_abs(int n);
int   tzd_atoi(tzd_ptr s);

#endif
