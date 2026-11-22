/* A member that is the whole struct (the kernel's pgd_t { pgdval_t pgd; })
   keeps the bounds of the array of wrappers: &top->pgd indexes it.  */
typedef struct { unsigned long pgd; } pgd_t;
pgd_t top[512];

int main(void)
{
    unsigned long *p = &top->pgd;
    p[511] = 7;
    return top[511].pgd == 7 ? 0 : 1;
}
