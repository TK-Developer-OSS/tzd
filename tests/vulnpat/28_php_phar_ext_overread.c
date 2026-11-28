/* 28  Search length not reduced by the start offset (CWE-125).
   PHP, CVE-2019-9021 (phar_detect_phar_fname_ext, NVD CVSS 3.0: 9.8).

   Shape: the name is held with its length (no terminator required).  To
   find the extension the code searches for '.' from the second byte, but
   passes the full length: the search reads one byte past the name, and
   whatever follows the name is taken as part of it.
   Here the name has no '.', and the next member of the record starts
   with one: the "extension" found is the next member's contents.

   GCC (any flags):  the search reads past the name and reports the
                     neighbouring member as the extension.
   TzD:              memchr checks its source's bounds (the 8-byte
                     member) and traps.  */
#include "vp.h"

struct entry {
    char name[8];                           /* length-counted */
    char secret[8];
};

int main(void)
{
    struct entry e;
    const char *dot;
    size_t len = 8;

    vp_begin("28_php_phar_ext_overread");
    memcpy(e.name, "archive1", 8);          /* no '.', no terminator */
    memcpy(e.secret, ".k3y!!!", 8);
#if VP_BAD
    dot = memchr(e.name + 1, '.', VP_RT(len));
#else
    dot = memchr(e.name + 1, '.', VP_RT(len) - 1);    /* the fix */
#endif
    vp_reached();
    if (dot != NULL && dot >= e.secret) {
        vp_effect("leaked", "the search read the next member");
        vp_show("ext", dot, 7);
    } else
        vp_effect("none", "no extension (the search stayed in the name)");
    return vp_end();
}
