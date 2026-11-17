/* GNU range designators [lo ... hi] = v (the kernel's hashtable and
   dumpstack initializers): task #42.  */
struct page { const char *name; int n; };
static int tbl[8] = { [1 ... 3] = 7, [5] = 1, 2 };
static const char *names[6] = { [0 ... 2] = "low", [3 ... 5] = "high" };
static struct page pages[4] = { [0 ... 3] = { .name = "p", .n = 4 } };

int main(void)
{
    int local[5] = { [0 ... 4] = 9 };
    if (tbl[0] || tbl[1] != 7 || tbl[3] != 7 || tbl[4] || tbl[5] != 1 || tbl[6] != 2) return 1;
    if (names[2][0] != 'l' || names[5][0] != 'h') return 2;
    if (pages[3].name[0] != 'p' || pages[2].n != 4) return 3;
    if (local[4] != 9 || local[0] != 9) return 4;
    return 0;
}
