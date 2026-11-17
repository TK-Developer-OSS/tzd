#include <string.h>
#include <stdlib.h>

/* A struct of pointers copied in as bytes (OpenSSH monitor_wrap.c
   mm_decode_activate_server_options): its pointer fields hold stale
   addresses with no bounds.  Testing them against null reads only the
   address, so it is allowed; the fields are then overwritten.  */
struct opts { char *a; char *b; int n; };

int main(void)
{
    struct opts src = { 0, 0, 7 }, *o;
    unsigned char bytes[sizeof src];
    long stale = 0x7f0012345000L;
    memcpy(bytes, &src, sizeof src);
    memcpy(bytes, &stale, sizeof stale);       /* a: set by the sender */
    o = calloc(1, sizeof *o);
    memcpy(o, bytes, sizeof *o);
    if (o->a != NULL)
        { o->a = malloc(2); o->a[0] = 'x'; o->a[1] = 0; }
    if (o->b)
        return 1;
    if (!o->a || o->n != 7 || o->a[0] != 'x')
        return 2;
    return 0;
}
