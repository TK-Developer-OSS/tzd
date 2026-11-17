#include <stdio.h>

/* Task #21 through a pointer: &p->f from a struct reached by p.  */
struct hdr {
    char tag[4];
    int kind;
    int secret;
};

int main(void)
{
    struct hdr h;
    struct hdr *p = &h;
    int *k = &p->kind;
    h.secret = 7;
    k[0] = 1;                 /* ok */
    k[1] = 99;                /* would overwrite p->secret: trap */
    printf("secret=%d\n", h.secret);
    return 0;
}
