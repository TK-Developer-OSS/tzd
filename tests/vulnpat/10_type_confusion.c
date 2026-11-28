/* 10  Type confusion: the handler trusts a type tag from the message
   (CWE-843).

   Shape: objects share a header with a type field; the dispatcher picks the
   handler from a tag that the sender controls instead of from the object,
   so a small object is handled as the large one, and the large one's name
   field is read past the small object's end, into the next heap block.
   The small object (8 bytes) gets a 24-byte glibc block; the 32-byte name
   read at offset 8 covers the rest of that block, the next header and the
   first 8 bytes of the secret allocated after it.

   GCC (any flags):  the reply carries the secret's bytes, the program
                     continues.
   TzD:              the pointer keeps the bounds of the 8-byte object it
                     came from; reading the name member traps.  */
#include "vp.h"

enum { T_SMALL = 1, T_BIG = 2 };

struct hdr   { int type; };
struct small { struct hdr h; int value; };
struct big   { struct hdr h; int value; char name[32]; };

static void handle(struct hdr *m, int tag, char *out)
{
#if VP_BAD
    if (tag == T_BIG) {                     /* tag from the message */
#else
    if (m->type == T_BIG) {                 /* type from the object */
#endif
        struct big *b = (struct big *) m;
        memcpy(out, b->name, sizeof b->name);
    } else {
        struct small *s = (struct small *) m;
        memset(out, 0, 32);
        out[0] = (char) ('0' + s->value);
    }
}

int main(void)
{
    struct small *s;
    char *secret;
    char out[32];
    int tag = VP_RT(T_BIG);

    vp_begin("10_type_confusion");
    s = malloc(sizeof *s);
    secret = malloc(24);
    strcpy(secret, "PRIVATE-DATA");
    s->h.type = T_SMALL;
    s->value = 7;
    handle(&s->h, tag, out);
    vp_reached();
    if (memcmp(out + 24, "PRIVATE-", 8) == 0) {
        vp_effect("leaked", "the small object was read as the large one");
        vp_show("name", out + 24, 8);
    } else
        vp_effect("none", "handled as the small object");
    return vp_end();
}
