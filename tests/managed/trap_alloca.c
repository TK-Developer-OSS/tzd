/* ... and not past them.  */
static volatile int i = 8;
int main (void)
{
    char *p = __builtin_alloca (8);
    p[i] = 1;
    return 0;
}
