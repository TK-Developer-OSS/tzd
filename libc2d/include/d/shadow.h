// SPDX-License-Identifier: MIT
/* libc2d: bounds of pointers stored in memory (src/shadow.c, task #1 (b)).  */
#ifndef D_SHADOW_H
#define D_SHADOW_H
#include <d/pointer.h>

tzd_ptr tzd_sh_load(const void *slot);
tzd_ptr tzd_sh_load_lazy(const void *slot);   /* #pragma tzd lazy_pointer */
/* #pragma tzd fnptr_holds */
void tzd_sh_record_typed(void *slot, tzd_ptr v, tzd_size_t id);
tzd_ptr tzd_sh_load_typed(const void *slot, tzd_size_t id);
void  tzd_sh_store(void *slot, tzd_ptr v);      /* writes the slot too */
void  tzd_sh_record(const void *slot, tzd_ptr v); /* slot already holds v.addr */
void  tzd_sh_copy(void *dst, const void *src, tzd_size_t n);
void  tzd_sh_clear(void *p, tzd_size_t n);
void  tzd_sh_wipe(void *p, tzd_size_t n);         /* overwrite twice + clear */
void  tzd_sh_release(void *p, tzd_size_t n);      /* range gone for good: table pages back */
void  tzd_sh_wipe_data(void *p, tzd_size_t n);    /* overwrite twice, no table work */
void  tzd_sh_init_static(void);
int   tzd_sh_peek(const void *slot, tzd_ptr *out);
int   tzd_sh_match(const void *slot);
#endif
