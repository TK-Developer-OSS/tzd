/* C99 variable length arrays: the size is evaluated where the array is
   declared, sizeof gives it, and the bounds are the run-time size.  */
static int fill (int n)
{
    char buf[n + 1];
    if (sizeof buf != (unsigned long) n + 1)
        return -1;
    for (int i = 0; i < n; i++)
        buf[i] = 'a' + i;
    buf[n] = 0;
    int s = 0;
    for (char *p = buf; *p; p++)
        s += *p - 'a';
    return s;
}

int main (void)
{
    int k = 4;
    long m[k][3];
    m[3][2] = 7;
    return fill (5) == 10 && m[3][2] == 7 ? 0 : 1;
}
