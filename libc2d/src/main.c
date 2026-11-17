// SPDX-License-Identifier: MIT
/* Process entry boundary for Managed C programs.

   cc1tzd emits the user's main() as __tzd_main.  Its argv/envp are
   managed pointers (values {addr, base, size}) to arrays of 8-byte pointer
   slots -- the same memory layout as C, so the arrays the C runtime (glibc's
   crt1 or libc2d's crt0) hands us are used in place.  Each slot gets its
   bounds registered (task #1 (b)): every string is bounded by its
   terminating NUL; the arrays by their terminating null slot.  Static
   initializers holding pointers are registered first.  Only linked when the
   program does not define a native main itself (libc2d's own tests do).  */
#include <d/pointer.h>
#include <d/shadow.h>

extern int __tzd_main(int argc, tzd_ptr argv, tzd_ptr envp);

static tzd_size_t str_size(const char *s)
{
    tzd_size_t n = 0;
    while (s[n])
        n++;
    return n + 1;
}

static tzd_ptr wrap_vector(char **v, tzd_size_t count)
{
    tzd_size_t i;
    for (i = 0; i < count; i++)
        tzd_sh_record(&v[i], tzd_ptr_make(v[i], str_size(v[i])));
    return tzd_ptr_make(v, (count + 1) * sizeof(char *));
}

int main(int argc, char **argv, char **envp)
{
    tzd_size_t envc = 0;
    tzd_ptr av, ev = tzd_ptr_null();
    tzd_sh_init_static();
    { extern void tzd_fn_init_static(void); tzd_fn_init_static(); }
    while (envp && envp[envc])
        envc++;
    av = wrap_vector(argv, (tzd_size_t) argc);
    if (envp)
        ev = wrap_vector(envp, envc);
    return __tzd_main(argc, av, ev);
}
