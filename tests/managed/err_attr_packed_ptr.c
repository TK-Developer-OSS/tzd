/* packed would put the pointer at offset 1: the bounds table keys 8-byte
   aligned pointer slots, so this is a compile error (task #26/#42).  */
struct p { char c; char *s; } __attribute__((packed));
int main(void){ return sizeof(struct p); }
