/* 17  Uninitialized pointer used after an ignored error (CWE-824 /
   CWE-457).

   Shape: lookup() sets its out parameter only on success; the caller
   ignores the return code and dereferences the pointer, which still holds
   whatever an earlier call left in that stack slot: here a pointer to
   another user's record.
   earlier() and handle() have the same frame shape, so at -O0 the slot
   usually holds earlier()'s pointer; with other flags the residue may be
   other bytes.  The faulty dereference itself always happens.

   GCC (any flags):  the dereference reads the residue (the other user's
                     id when the frames line up) or crashes on garbage.
   TzD:              an automatic pointer without an initializer is the
                     null pointer, never stack residue; the dereference
                     traps as a null access.  */
#include "vp.h"

struct user { int id; char name[12]; };

static struct user users[2] = { { 1, "alice" }, { 2, "bob" } };
static struct user admin = { 0, "root" };

__attribute__((noinline)) static int lookup(struct user **out, int id)
{
    if (id < 0 || id >= 2)
        return -1;                          /* *out not set */
    *out = &users[id];
    return 0;
}

__attribute__((noinline)) static int earlier(void)
{
    struct user *p = &admin;
    struct user **pp = &p;                  /* keep p in its stack slot */
    return (*pp)->id;
}

__attribute__((noinline)) static int handle(int id)
{
    struct user *u;
    struct user **pu = &u;                  /* u lives in its stack slot */
#if VP_BAD
    (void) lookup(pu, id);                  /* error ignored */
#else
    if (lookup(pu, id) < 0)
        return -1;
#endif
    return (*pu)->id;
}

int main(void)
{
    int id;

    vp_begin("17_uninit_pointer");
    (void) earlier();
    id = handle(VP_RT(5));                  /* no such user */
    if (id == -1)
        return vp_end();
    vp_reached();
    if (id == 0)
        vp_effect("confused", "the unset pointer reads the admin record");
    else
        vp_effect("none", "the unset pointer reads other bytes");
    return vp_end();
}
