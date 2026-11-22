/* TzD stdlib (Managed C / TzD view, libc2d mode).  malloc returns a pointer
   object carrying the allocation's bounds; free wipes the block (task #1).  */
#ifndef _TZD_STDLIB_H
#define _TZD_STDLIB_H
typedef __SIZE_TYPE__ size_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
void *malloc(size_t n)                 __asm__("tzd_malloc");
void *calloc(size_t n, size_t size)    __asm__("tzd_calloc");
void *realloc(void *p, size_t n)       __asm__("tzd_realloc");
void  free(void *p)                    __asm__("tzd_free");
void  qsort(void *base, size_t n, size_t size,
            int (*cmp)(const void *, const void *)) __asm__("tzd_qsort");
void  exit(int status)                 __asm__("tzd_exit");
void  abort(void)                      __asm__("tzd_abort");
int   abs(int n)                       __asm__("tzd_abs");
int   atoi(const char *s)              __asm__("tzd_atoi");
#endif
