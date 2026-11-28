/* 04  Over-read driven by a length field (CWE-126), the Heartbleed shape
   (CVE-2014-0160).

   Shape: an echo request carries its own payload length; the reply copies
   that many bytes from the request without comparing it with what was
   actually received.  The request buffer sits next to key material, and
   the reply carries the key out.
   The request and the key are adjacent members of one session record, so
   the GCC build reads them with no heap layout involved.

   GCC (any flags):  the reply contains the key, the program continues.
                     The read stays inside the session struct, so
                     _FORTIFY_SOURCE (whole-object size for memcpy) does not
                     object.
   TzD:              the request member carries its own bounds (16 bytes);
                     memcpy checks the source range and traps.  */
#include "vp.h"

struct session {
    char req[16];               /* received: 2-byte length + payload */
    char key[16];               /* must never leave the process */
};

int main(void)
{
    struct session s;
    char *reply;
    size_t got = 4;                         /* payload bytes received */
    size_t claimed;

    vp_begin("04_overread_length_field");
    memcpy(s.key, "TOPSECRETKEY1234", 16);
    memset(s.req, 0, sizeof s.req);
    s.req[0] = 0;
    s.req[1] = (char) VP_RT(VP_BAD ? 24 : 4);   /* claimed length */
    memcpy(s.req + 2, "ping", 4);
    claimed = (size_t) (unsigned char) s.req[1];

#if !VP_BAD
    if (claimed > got)
        return vp_end();                    /* drop the request */
#endif
    reply = malloc(claimed);
    memcpy(reply, s.req + 2, claimed);
    vp_reached();
    if (claimed > got && memcmp(reply + 14, "TOPSECRETK", 10) == 0) {
        vp_effect("leaked", "the reply carries the key");
        vp_show("reply", reply, (int) claimed);
    } else
        vp_effect("none", "the reply holds only the payload");
    free(reply);
    return vp_end();
}
