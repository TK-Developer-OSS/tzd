/* 08  Unescape loop runs past a trailing backslash (CWE-122), the shape of
   the sudo "Baron Samedit" bug (CVE-2021-3156).

   Shape: the destination is sized by strlen(arg) + 1.  The loop drops a
   backslash and copies the character after it; when the backslash is the
   last character, the character after it is the terminator, the loop
   steps over it and keeps copying whatever follows in memory.
   Here the argument is followed by the next argument in one contiguous
   argument area (as argv strings are), so the read stays inside that area
   and only the write leaves its buffer.  The 4-byte argument gets a
   24-byte glibc block; the 44 bytes written run over the next block's
   header and its first 12 bytes.  Nothing is freed afterwards.

   GCC (any flags):  the neighbour's text changes, the program continues.
   TzD:              the destination carries the bounds of its 5 bytes;
                     the store to dst[5] traps.  The read past the
                     terminator is inside the argument area and is legal.  */
#include "vp.h"

/* "abc\" then the next argument: 40 'X' and a terminator.  */
static char argarea[64] =
    "abc\\\0" "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

static void unescape(char *dst, const char *from)
{
    char *to = dst;
    while (*from) {
#if VP_BAD
        if (from[0] == '\\' && from[1] != ' ')
#else
        if (from[0] == '\\' && from[1] != ' ' && from[1] != '\0')
#endif
            from++;
        *to++ = *from++;
    }
    *to = 0;
}

int main(void)
{
    char *arg = argarea + VP_RT(0);
    char *dst, *next;

    vp_begin("08_unescape_overrun");
    dst = malloc(strlen(arg) + 1);          /* 5 bytes */
    next = malloc(24);
    strcpy(next, "neighbour-block");
    unescape(dst, arg);
    vp_reached();
    if (strcmp(next, "neighbour-block") != 0) {
        vp_effect("corrupted", "the unescape ran past its buffer");
        vp_show("neighbour", next, 12);
    } else
        vp_effect("none", "the next heap block is intact");
    return vp_end();
}
