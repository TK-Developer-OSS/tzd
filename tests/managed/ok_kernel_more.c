/* More kernel idioms (task #42): fallthrough; as a statement, GNU a ?: b
   (a evaluated once), extern void linker symbols (address only),
   &x of an extern array of unknown size, __builtin_frame_address.  */
extern char ext_arr[];
char ext_arr[6] = "hello";
static int calls;
static int once(int v) { calls++; return v; }

int main(void)
{
    int r = 0;
    char *p = 0, buf[2] = "x";
    switch (once(1)) {
    case 1:
        r += 1;
        __attribute__((__fallthrough__));
    case 2:
        r += 2;
        break;
    }
    if (r != 3 || calls != 1) return 1;
    if ((once(5) ?: 9) != 5 || calls != 2 || (once(0) ?: 9) != 9 || calls != 3) return 2;
    if ((p ?: buf)[0] != 'x') return 3;
    if ((*&ext_arr)[4] != 'o' || sizeof(*&ext_arr) != 6) return 4;
    if (!__builtin_frame_address(0)) return 5;
    return 0;
}
