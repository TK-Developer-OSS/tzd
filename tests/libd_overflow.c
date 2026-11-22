/* Writes one byte past the end of a 16-byte array: must trap (exit 134). */
#include <d/stdio.h>

int main(void) {
    char buf[16];
    tzd_ptr p = tzd_ptr_make(buf, sizeof buf);
    tzd_ptr_store8(tzd_ptr_add(p, 15), 'x');     /* last valid byte */
    tzd_puts(tzd_ptr_make("ok: index 15", sizeof "ok: index 15"));
    tzd_ptr_store8(tzd_ptr_add(p, 16), 'x');     /* one past the end */
    tzd_puts(tzd_ptr_make("NOT REACHED", sizeof "NOT REACHED"));
    return 0;
}
