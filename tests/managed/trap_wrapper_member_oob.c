/* The wrapper's member keeps the array's bounds, not more.  */
typedef struct { unsigned long pgd; } pgd_t;
pgd_t top[512];

int main(void)
{
    unsigned long *p = &top->pgd;
    p[512] = 7;
    return 0;
}
