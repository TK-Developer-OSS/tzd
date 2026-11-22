/* __tzd_narrow (p, n): bounds [p, p + n) inside p's own (task #13).  */
static char block[64];
static char *take(int off, int n) { return __tzd_narrow(block + off, n); }
int main(void)
{
    char *a = take(0, 8), *b = take(8, 8);
    for (int i = 0; i < 8; i++) a[i] = 'a', b[i] = 'b';
    if (block[7] != 'a' || block[8] != 'b') return 1;
    char *z = __tzd_narrow(block + 64, 0);     /* empty, at the end: allowed */
    (void) z;
    return 0;
}
