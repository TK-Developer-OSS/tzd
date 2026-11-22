/* __tzd_narrow cannot widen: a size beyond the current bounds traps.  */
int main(void)
{
    char small[4];
    char *p = __tzd_narrow(small, 100);
    return p[0];
}
