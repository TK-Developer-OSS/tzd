/* The result pointer of __builtin_add_overflow must reach sizeof *d: a
   pointer to the last byte of a buffer traps before GCC writes 8 bytes.  */
int main(void)
{
    char buf[8];
    long *d = (long *) (buf + 4);
    return __builtin_add_overflow(1L, 2L, d);   /* trap */
}
