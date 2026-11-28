/* 09  strncpy leaves the string unterminated, then a read runs past it
   (CWE-170 -> CWE-126).

   Shape: strncpy(dst, src, sizeof dst) with a source exactly as long as
   the buffer writes no terminator.  The next strlen / "%s" keeps reading
   into the member after it, and the session token goes out with the name.

   GCC (any flags):  strlen counts into the token, the reply carries it,
                     the program continues.  The read is inside the struct.
   TzD:              name carries its own bounds (8 bytes); strlen reaches
                     the end of them without a terminator and traps.  */
#include "vp.h"

struct user {
    char name[8];
    char token[16];
};

static const char *const names[] = { "guest", "operator" };

int main(void)
{
    struct user u;
    char reply[64];
    size_t n;

    vp_begin("09_strncpy_unterminated");
    strcpy(u.token, "SESSIONTOKEN42");
    strncpy(u.name, names[VP_RT(VP_BAD)], sizeof u.name);
#if !VP_BAD
    u.name[sizeof u.name - 1] = 0;
#endif
    n = strlen(u.name);
    vp_reached();
    if (n >= sizeof u.name) {
        memcpy(reply, u.name, n + 1);
        vp_effect("leaked", "the name runs on into the token");
        vp_show("reply", reply, (int) n);
    } else
        vp_effect("none", "the name is terminated");
    return vp_end();
}
