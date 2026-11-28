/* 21  Element length from the frame copied into a fixed field (CWE-120).
   Linux kernel, CVE-2019-17133 (cfg80211 wext SSID, NVD CVSS 3.1: 9.8).

   Shape: a network's name arrives as an information element, a type
   byte, a length byte and the bytes.  The standard caps the name at 32
   bytes; the code copies "length" bytes into a 32-byte field without
   checking, and the length byte can say up to 255.

   GCC (any flags):  the members after the field are rewritten (here the
                     length and the "connected" flag), the program
                     continues.
   TzD:              the destination is the 32-byte member: memcpy checks
                     its bounds and traps before writing.  */
#include "vp.h"

struct wdev {
    unsigned char ssid[32];
    unsigned char ssid_len;
    unsigned char connected;
    char iface[14];
};

static unsigned char ie[2 + 40];            /* element: id, len, bytes */

int main(void)
{
    struct wdev w;
    unsigned int n;

    vp_begin("21_linux_ssid_len");
    memset(&w, 0, sizeof w);
    strcpy(w.iface, "wlan0");
    ie[0] = 0;                              /* SSID element */
    ie[1] = (unsigned char) VP_RT(VP_BAD ? 40 : 12);
    memset(ie + 2, 'N', 40);

    n = ie[1];
#if !VP_BAD
    if (n > sizeof w.ssid)                  /* the fix: cap at 32 */
        n = sizeof w.ssid;
#endif
    memcpy(w.ssid, ie + 2, n);
    w.ssid_len = (unsigned char) (n < 32 ? n : 32);
    vp_reached();
    if (w.connected != 0 || strcmp(w.iface, "wlan0") != 0) {
        vp_effect("corrupted", "the name ran over the following members");
        vp_show("iface", w.iface, 5);
    } else
        vp_effect("none", "the following members are intact");
    return vp_end();
}
