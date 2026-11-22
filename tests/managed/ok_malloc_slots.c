/* libc2d's small-block allocator (task #66): fixed-size slots, exact
   bounds, addresses never used again, contents survive realloc, and many
   malloc/free cycles of every size class keep working (runs whose slots
   are all freed go back to the kernel).  */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

int main(void)
{
    static const int sizes[] = { 1, 16, 17, 48, 64, 100, 200, 500, 1000, 2048, 2049, 5000 };
    char *keep[12];
    uintptr_t prev[12];
    for (int k = 0; k < 12; k++) {
        keep[k] = malloc(sizes[k]);
        if (!keep[k]) return 1;
        memset(keep[k], 'a' + k, sizes[k]);
        prev[k] = 0;
    }
    for (int round = 0; round < 3000; round++)
        for (int k = 0; k < 10; k++) {          /* the slot classes */
            char *p = calloc(1, sizes[k]);
            if (!p) return 2;
            for (int i = 0; i < sizes[k]; i++)
                if (p[i]) return 3;             /* fresh memory is zero */
            if ((uintptr_t) p <= prev[k]) return 4;   /* never reused */
            if (((uintptr_t) p & 15) != 0) return 5;
            prev[k] = (uintptr_t) p;
            p[0] = 1; p[sizes[k] - 1] = 2;
            free(p);
        }
    for (int k = 0; k < 12; k++) {
        for (int i = 0; i < sizes[k]; i++)
            if (keep[k][i] != 'a' + k) return 6;
        keep[k] = realloc(keep[k], sizes[k] * 3);
        if (!keep[k]) return 7;
        for (int i = 0; i < sizes[k]; i++)
            if (keep[k][i] != 'a' + k) return 8;
        free(keep[k]);
    }
    puts("ok");
    return 0;
}
