/* __builtin_memcpy goes to libc2d's checked memcpy: copying 64 bytes into
   a 4-byte array traps before anything is written.  */
int main(void)
{
    char a[4], b[64];
    __builtin_memset(b, 'x', sizeof b);
    __builtin_memcpy(a, b, 64);
    return a[0];
}
