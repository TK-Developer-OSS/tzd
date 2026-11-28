/* 20  Key length from the message, buffer sized by the message (CWE-787).
   Linux kernel, CVE-2021-43267 (TIPC MSG_CRYPTO, NVD CVSS 3.1: 9.8).

   Shape: a key-exchange message carries an algorithm name, a key length
   and the key.  The receiver allocates a key record sized by the
   message's data size, then copies "key length" bytes taken from the
   message into it.  Nothing checks that the stated key length fits in
   what was received.
   The record and the next allocation are two fresh 40-byte requests,
   48 bytes apart under glibc; a key length of 48 runs into the second.

   GCC (any flags):  the neighbouring record is rewritten, the program
                     continues.
   TzD:              memcpy checks the destination's bounds and traps
                     before writing.  */
#include "vp.h"

struct key_rec {
    char alg[8];
    unsigned int keylen;
    char key[];                             /* keylen bytes */
};

/* The received message: alg[8], keylen (4 bytes), key.  */
static unsigned char msg[64] = "aes-gcm";

int main(void)
{
    struct key_rec *k;
    char *next;
    unsigned int keylen;
    size_t datasz = 40;                     /* size of the message's data */

    vp_begin("20_linux_tipc_key_len");
    memset(msg + 12, 'K', 52);
    keylen = VP_RT(VP_BAD ? 48 : 20);       /* stated by the sender */
    memcpy(msg + 8, &keylen, 4);

    k = malloc(datasz);
    next = malloc(40);
    strcpy(next, "peer-state");

    memcpy(k->alg, msg, 8);
    memcpy(&k->keylen, msg + 8, 4);
#if !VP_BAD
    if (k->keylen > datasz - sizeof *k)     /* the fix: check the length */
        return vp_end();
#endif
    memcpy(k->key, msg + 12, k->keylen);
    vp_reached();
    if (strcmp(next, "peer-state") != 0) {
        vp_effect("corrupted", "the key copy ran into the next heap block");
        vp_show("next", next, 10);
    } else
        vp_effect("none", "the next heap block is intact");
    return vp_end();
}
