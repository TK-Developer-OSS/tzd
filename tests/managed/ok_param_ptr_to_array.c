/* A parameter that is a pointer to arrays keeps its array size
   (`const u32 (*tab)[256]`, the kernel's crc32_body); only a
   parameter's own outer array dimension is dropped (ok_param_2d.c).  */
typedef unsigned int u32;
static u32 tables[2][256];
static u32 sum (const u32 (*tab)[256], int n)
{
    const u32 *t0 = tab[0], *t1 = tab[1];
    return t0[n] + t1[n] + (u32) sizeof tab[0];
}
int main (void)
{
    tables[0][5] = 1;
    tables[1][5] = 2;
    return sum (tables, 5) == 3 + 1024 ? 0 : 1;
}
