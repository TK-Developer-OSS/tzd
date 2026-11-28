/* 18  Uninitialized heap memory sent out (CWE-908 -> CWE-200).

   Shape: a reply buffer is allocated with malloc, only its header is
   filled, and the whole buffer is sent.  The rest still holds what the
   previous owner of the block left: here key material that was freed
   without being cleared.
   glibc returns the freed 64-byte block to the next 64-byte request; its
   free-list fields cover only the first 16 bytes, so the key at offset 32
   survives.

   GCC (any flags):  the reply carries the key, the program continues.
   TzD:              free wipes the block (zero, then random bytes) and its
                     addresses are never handed out again, so the reply
                     cannot hold the old key.  Reading uninitialized heap
                     bytes is not itself a trap: the expected result is
                     "ran, nothing leaked", not a stop.  */
#include "vp.h"

int main(void)
{
    char *key, *reply;

    vp_begin("18_uninit_heap_leak");
    key = malloc(64);
    memset(key, 0, 64);
    strcpy(key + 32, "PRIVATE-KEY-MATERIAL");
    free(key);                              /* not cleared */

#if VP_BAD
    reply = malloc(VP_RT(64));
#else
    reply = calloc(1, VP_RT(64));           /* the fix: zeroed */
#endif
    memcpy(reply, "OK", 3);                 /* header only */
    vp_reached();
    if (memcmp(reply + 32, "PRIVATE-KEY", 11) == 0) {
        vp_effect("leaked", "the reply carries the previous owner's key");
        vp_show("reply+32", reply + 32, 20);
    } else
        vp_effect("none", "the reply holds no old data");
    free(reply);
    return vp_end();
}
