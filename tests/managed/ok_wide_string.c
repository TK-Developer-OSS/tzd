/* Wide string literals (L"..."), also concatenated with narrow ones and
   as array initializers, are arrays of wchar_t (they were empty strings).  */
typedef __WCHAR_TYPE__ wchar_t;
static wchar_t g[] = L"xyz";

int main (void)
{
    wchar_t buf[] = L"ab" "cd";
    const wchar_t *p = L"hello";
    if (sizeof (L"AAAA") != 5 * sizeof (wchar_t) || sizeof buf != 5 * sizeof (wchar_t))
        return 1;
    if (buf[3] != L'd' || buf[4] != 0 || p[4] != L'o' || g[2] != L'z')
        return 2;
    int n = 0;
    while (p[n])
        n++;
    return n == 5 ? 0 : 3;
}
