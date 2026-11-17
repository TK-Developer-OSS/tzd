/* hello.c in hand-lowered libc2d form: fixed array -> pointer object ->
   build the string by character-code arithmetic -> puts.  */
#include <d/stdio.h>

int main(void) {
    char buf[16];
    tzd_ptr p = tzd_ptr_make(buf, sizeof buf);
    tzd_ptr q = p;

    tzd_ptr_store8(q, 'A' + 7);  q = tzd_ptr_add(q, 1);   /* H */
    tzd_ptr_store8(q, 'a' + 4);  q = tzd_ptr_add(q, 1);   /* e */
    tzd_ptr_store8(q, 'a' + 11); q = tzd_ptr_add(q, 1);   /* l */
    tzd_ptr_store8(q, 'a' + 11); q = tzd_ptr_add(q, 1);   /* l */
    tzd_ptr_store8(q, 'a' + 14); q = tzd_ptr_add(q, 1);   /* o */
    tzd_ptr_store8(q, ',');      q = tzd_ptr_add(q, 1);
    tzd_ptr_store8(q, ' ');      q = tzd_ptr_add(q, 1);
    tzd_ptr_store8(q, 'A' + 22); q = tzd_ptr_add(q, 1);   /* W */
    tzd_ptr_store8(q, 'a' + 14); q = tzd_ptr_add(q, 1);   /* o */
    tzd_ptr_store8(q, 'a' + 17); q = tzd_ptr_add(q, 1);   /* r */
    tzd_ptr_store8(q, 'a' + 11); q = tzd_ptr_add(q, 1);   /* l */
    tzd_ptr_store8(q, 'a' + 3);  q = tzd_ptr_add(q, 1);   /* d */
    tzd_ptr_store8(q, '!');      q = tzd_ptr_add(q, 1);
    tzd_ptr_store8(q, 0);

    if (tzd_ptr_diff(q, p) != 13)
        return 1;

    tzd_puts(p);
    return 0;
}
