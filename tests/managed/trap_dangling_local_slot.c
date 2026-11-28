/* A pointer to a local kept in memory after the local's function returned
   (CWE-562): the slot is marked when the local is released, and the next
   load of it traps (task #8).  */
__attribute__((noinline)) static void make(char **out, char c)
{
    char buf[16];
    buf[0] = c;
    *out = buf;
}
int main(void)
{
    char *first = 0, *second = 0;
    make(&first, 'h');
    make(&second, 'Z');
    return first[0];
}
