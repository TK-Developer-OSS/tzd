/* A pointer member set by a designator keeps the bounds of what it points to.  */
struct s { int n; char *p; };
int main(void)
{
    char small[2] = "x";
    struct s v = { .p = small, .n = 1 };
    return v.p[5];
}
