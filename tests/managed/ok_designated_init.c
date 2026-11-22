/* C99 designated initializers.  */
struct pt { int x, y; };
struct box { struct pt a, b; char *name; int tag[3]; };
union u { int i; char c[4]; };
struct anon { int k; union { int ui; float uf; }; int last; };
static struct box sb = { .b.y = 4, .name = "static", .tag[2] = 9 };
static int sa[] = { [4] = 40, [1] = 10, 11 };
int main(void)
{
    char buf[] = "local";
    struct box b = { .name = buf, .a = { 1, 2 }, .b = { .x = 3 }, .tag = { [1] = 7 } };
    int a[6] = { 1, [3] = 3, 4, [0] = 9 };
    union u un = { .c = { 'a', 'b' } };
    struct anon an = { .ui = 5, .last = 6 };
    struct pt p = { .y = 2, .x = 1 };
    if (b.a.x != 1 || b.a.y != 2 || b.b.x != 3 || b.b.y != 0) return 1;
    if (b.name[0] != 'l' || b.tag[0] || b.tag[1] != 7 || b.tag[2]) return 2;
    if (a[0] != 9 || a[1] || a[2] || a[3] != 3 || a[4] != 4 || a[5]) return 3;
    if (un.c[0] != 'a' || un.c[1] != 'b') return 4;
    if (an.k || an.ui != 5 || an.last != 6) return 5;
    if (p.x != 1 || p.y != 2) return 6;
    if (sb.b.y != 4 || sb.a.x || sb.name[0] != 's' || sb.tag[2] != 9) return 7;
    if (sizeof sa != 5 * sizeof(int) || sa[1] != 10 || sa[2] != 11 || sa[4] != 40 || sa[0]) return 8;
    return 0;
}
