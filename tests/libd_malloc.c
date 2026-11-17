/* tzd_malloc: the pointer carries its allocation's bounds.  */
#include <d/stdio.h>
#include <d/stdlib.h>
#include <d/string.h>

int main(void) {
    tzd_ptr p = tzd_malloc(11);
    tzd_ptr z = tzd_calloc(4, 4);
    int i;

    for (i = 0; i < 10; i++)
        tzd_ptr_store8(tzd_ptr_add(p, i), 'a' + i);
    tzd_ptr_store8(tzd_ptr_add(p, 10), 0);          /* last valid byte */

    if (tzd_ptr_load8(z) != 0 || tzd_ptr_load8(tzd_ptr_add(z, 15)) != 0)
        return 1;                                /* calloc not zeroed */

    tzd_puts(p);                                   /* abcdefghij */
    tzd_printf(tzd_ptr_make("len=%d\n", 8), (int) tzd_strlen(p));
    tzd_free(p);
    tzd_free(z);

    tzd_free(tzd_ptr_null());                        /* free(NULL) is fine */
    tzd_puts(tzd_ptr_make("freed", 6));

    tzd_ptr_store8(tzd_ptr_add(tzd_malloc(4), 4), 'x'); /* one past -> trap */
    tzd_puts(tzd_ptr_make("NOT REACHED", 12));
    return 0;
}
