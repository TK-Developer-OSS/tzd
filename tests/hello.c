#include <stdio.h>

/* Managed C hello: fixed array -> pointer -> build the string by
   character-code arithmetic -> puts.  */
int main(void) {
    char buf[16];
    char *p = buf;

    *p++ = 'A' + 7;    /* H */
    *p++ = 'a' + 4;    /* e */
    *p++ = 'a' + 11;   /* l */
    *p++ = 'a' + 11;   /* l */
    *p++ = 'a' + 14;   /* o */
    *p++ = ',';
    *p++ = ' ';
    *p++ = 'A' + 22;   /* W */
    *p++ = 'a' + 14;   /* o */
    *p++ = 'a' + 17;   /* r */
    *p++ = 'a' + 11;   /* l */
    *p++ = 'a' + 3;    /* d */
    *p++ = '!';
    *p = 0;

    if (p - buf != 13)
        return 1;

    puts(buf);
    return 0;
}
