/* C99 compound literals.  */
struct pt { int x, y; };
struct named { const char *s; int n; };
static int sum(const int *a, int n) { int t = 0; for (int i = 0; i < n; i++) t += a[i]; return t; }
static int px(struct pt *p) { return p->x * 10 + p->y; }
static int *gp = (int[]){ 5, 6, 7 };          /* file scope: static object */
static int calls;
static int next(void) { return ++calls; }
int main(void)
{
    int total = 0;
    for (int i = 0; i < 3; i++)
        total += ((int[]){ i, i * 2 })[1];     /* fresh each iteration */
    if (total != 6) return 1;
    if (sum((int[]){ 1, 2, 3, 4 }, 4) != 10) return 2;
    if (px(&(struct pt){ .y = 2, .x = 1 }) != 12) return 3;
    if ((struct pt){ 3, 4 }.y != 4) return 4;
    struct named *np = &(struct named){ "hey", 3 };
    if (np->s[2] != 'y' || np->n != 3) return 5;
    if (gp[2] != 7) return 6;
    int *q = (int[]){ next(), next() };         /* initializers run once */
    if (calls != 2 || q[0] != 1 || q[1] != 2) return 7;
    char *s = (char[]){ "abc" };
    if (s[1] != 'b') return 8;
    return 0;
}
