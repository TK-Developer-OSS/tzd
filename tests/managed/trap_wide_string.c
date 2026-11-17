/* ... and they have their own bounds.  */
typedef __WCHAR_TYPE__ wchar_t;
static volatile int i = 6;

int main (void)
{
    const wchar_t *p = L"hello";
    return (int) p[i];
}
