/* The pointer is at offset 0 but packed makes the struct 9 bytes: in an
   array of them the second element's slot is at offset 9: a compile
   error.  */
struct p { char *s; char c; } __attribute__((packed));
int main(void) { struct p a[2] = { 0 }; return a[1].c; }
