/* 24  Unchecked error in a loop that writes one element per pass
   (CWE-252 -> CWE-787).
   OpenSSL, CVE-2016-2182 (BN_bn2dec, NVD CVSS 3.0: 9.8).

   Shape: converting a big number to decimal, the code sizes an array
   from the number's length, then repeatedly divides by 10^k and stores
   each remainder.  The division can fail (here: on an oversize number)
   and then leaves the number unchanged; the return value is not
   checked, so the loop goes on storing past the end of the array.
   Here the array holds 2 entries and the record after it in the same
   allocation is the output string's length.

   GCC (any flags):  the stores run past the array; the record after it
                     is rewritten (and, with no end to the loop, the
                     process eventually crashes).
   TzD:              the first store past the array traps.  */
#include "vp.h"

#define ERR ((unsigned long) -1)
#define LIMIT 4                             /* words a division handles */

struct bn {
    int top;                                /* words in use */
    unsigned long d[8];
};

static unsigned long div_word(struct bn *a, unsigned long w)
{
    unsigned long long rem = 0;
    int i;
    if (a->top > LIMIT)
        return ERR;                         /* a unchanged */
    for (i = a->top - 1; i >= 0; i--) {
        unsigned long long v = (rem << 32) | (a->d[i] & 0xffffffffu);
        a->d[i] = (unsigned long) (v / w);
        rem = v % w;
    }
    while (a->top > 0 && a->d[a->top - 1] == 0)
        a->top--;
    return (unsigned long) rem;
}

struct out {
    unsigned long words[2];                 /* sized from the estimate */
    unsigned long outlen;
};

int main(void)
{
    struct bn t;
    struct out *o;
    unsigned long *lp;
    int i;

    vp_begin("24_openssl_bn2dec_unchecked");
    memset(&t, 0, sizeof t);
    t.top = VP_RT(6);                       /* oversize number */
    for (i = 0; i < t.top; i++)
        t.d[i] = 7;
    o = malloc(sizeof *o);
    o->outlen = 12;
    lp = o->words;
    for (i = 0; t.top != 0 && i < 3; i++) { /* (3: keep the demo finite) */
        unsigned long r = div_word(&t, 10000);
#if !VP_BAD
        if (r == ERR)                       /* the fix: check the error */
            break;
#endif
        *lp++ = r;
    }
    vp_reached();
    if (o->outlen != 12) {
        vp_effect("corrupted", "the stores ran past the array into outlen");
    } else
        vp_effect("none", "outlen is intact");
    free(o);
    return vp_end();
}
