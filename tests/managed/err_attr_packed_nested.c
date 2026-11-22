/* A pointer nested in a packed struct's member at offset 4 (the kernel's
   compat_group_req with sockaddr_storage's alignment-only void *): the
   slot is off an 8-byte boundary, so this is a compile error (TK
   2026-11-13: stop at compile time as well as at run time).  */
struct storage { union { struct { unsigned short family; char data[14]; }; void *align; }; };
struct compat_req { unsigned int iface; struct storage group __attribute__((aligned(4))); } __attribute__((packed));
int main(void) { struct compat_req req = { 0 }; return req.group.family; }
