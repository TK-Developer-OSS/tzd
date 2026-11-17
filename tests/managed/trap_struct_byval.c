/* The pointer inside a struct returned and passed by value keeps its
   bounds: an access past the 4-byte buffer traps in the callee.  */
typedef struct { char *p; } box_t;
static box_t box(char *p) { box_t b = { p }; return b; }
static char at(box_t b, int i) { return b.p[i]; }

int main(void)
{
    char buf[4] = "abc";
    return at(box(buf), 6);      /* trap */
}
