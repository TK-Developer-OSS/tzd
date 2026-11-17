/* memcpy/strcpy go to libc2d's checked versions: the overflow is caught
   inside the copy, before the neighbouring member is touched (the shape
   of most C string CVEs).  */
#include <string.h>
#include <stdio.h>

struct req { char host[16]; int authorized; };

int main(void)
{
    struct req r;
    const char *evil = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    r.authorized = 0;
    strcpy(r.host, evil);
    printf("authorized=%d\n", r.authorized);
    return 0;
}
