/* Statement expressions keep their value although their pointer locals
   are wiped at the end (task #31).  */
struct pair { char *s; int n; };
static int len(const char *s) { int n = 0; while (s[n]) n++; return n; }
int main(void)
{
    char buf[8] = "hello";
    char **keep;
    int a = ({ char *p = buf; keep = &p; len(p); });
    char c = ({ char *q = buf + 1; char **qq = &q; **qq; });
    int b = ({ struct pair pr; pr.s = buf; pr.n = 2; pr.s[pr.n]; });
    char *r = ({ char *t = buf + 4; char **tt = &t; *tt; });
    (void) keep;
    if (a != 5 || c != 'e' || b != 'l' || *r != 'o')
        return 1;
    return 0;
}
