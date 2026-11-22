/* Writing past a narrowed part stops at its own end, not the block's.  */
static char block[64];
int main(void)
{
    char *a = __tzd_narrow(block, 8);
    a[8] = 'x';           /* would be inside block, outside a */
    return 0;
}
