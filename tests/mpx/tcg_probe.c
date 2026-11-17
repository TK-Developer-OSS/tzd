/* Probe for the MPX emulation of the machine under test (plain C, no TzD):
   enables MPX the way libc2d's tzd_mpx_init does, then runs the three
   instructions of a -ftzd-mpx check and looks at what follows them.
   Built by the host GCC, static; tests/mpx/run_tcg.sh runs it in the
   guest.  Prints one line per step.  */
#include <stdio.h>
#include <string.h>
#include <cpuid.h>

static unsigned char area[8192] __attribute__((aligned(64)));

int main(int argc, char **argv)
{
    unsigned a, b, c, d, off;
    unsigned long x = 0x00007ffc12345678UL, y, z, lo, hi;
    volatile long obj[4] = { 1, 2, 3, 4 };
    __cpuid_count(7, 0, a, b, c, d);
    printf("cpuid mpx: %u\n", (b >> 14) & 1);
    __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    printf("xcr0: %#lx\n", lo);
    __cpuid_count(0xd, 4, a, b, c, d);
    off = b;
    printf("bndcsr offset: %u\n", off);
    if (argc > 1 && strcmp(argv[1], "noenable") == 0)
        puts("MPX left disabled");
    else {
        *(unsigned long *) (area + 512) = 0x10;
        *(unsigned long *) (area + off) = 1;
        __asm__ volatile ("xrstor (%0)" : : "r"(area), "a"(0x10), "d"(0) : "memory");
        *(unsigned long *) (area + off) = 0;
        *(unsigned long *) (area + 512) = 0;
        __asm__ volatile ("xsave (%0)" : : "r"(area), "a"(0x10), "d"(0) : "memory");
        printf("bndcfgu after enabling: %#lx\n", *(unsigned long *) (area + off));
    }
    __asm__ volatile ("lea 0(%1), %0" : "=r"(y) : "r"(x));
    printf("lea before any MPX instruction: %s\n", y == x ? "64-bit" : "TRUNCATED");
    __asm__ volatile ("bndmk -1(%0,%1), %%bnd0" : : "r"((unsigned long) obj), "r"(32UL));
    __asm__ volatile ("lea 0(%1), %0" : "=r"(y) : "r"(x));
    printf("lea after bndmk: %s (%#lx)\n", y == x ? "64-bit" : "TRUNCATED", y);
    __asm__ volatile ("bndcl %0, %%bnd0" : : "r"((unsigned long) obj));
    __asm__ volatile ("lea 0(%1), %0" : "=r"(y) : "r"(x));
    printf("lea after bndcl: %s (%#lx)\n", y == x ? "64-bit" : "TRUNCATED", y);
    __asm__ volatile ("bndcu %0, %%bnd0" : : "r"((unsigned long) obj + 7));
    __asm__ volatile ("lea 0(%1), %0" : "=r"(y) : "r"(x));
    printf("lea after bndcu (register form): %s (%#lx)\n", y == x ? "64-bit" : "TRUNCATED", y);
    if (argc > 1 && strcmp(argv[1], "trap") == 0) {
        puts("now an out-of-bounds bndcu, register form (expect #BR -> SIGSEGV)");
        fflush(stdout);
        /* bndmk right before: a call (printf) resets the bound registers
           while BNDPRESERVE is clear.  */
        __asm__ volatile ("bndmk -1(%0,%1), %%bnd0; bndcu %2, %%bnd0"
                          : : "r"((unsigned long) obj), "r"(32UL), "r"((unsigned long) obj + 32));
        puts("NOT TRAPPED");
    }
    if (argc > 1 && strcmp(argv[1], "low") == 0) {
        puts("now an out-of-bounds bndcl, register form (expect #BR -> SIGSEGV)");
        fflush(stdout);
        __asm__ volatile ("bndmk -1(%0,%1), %%bnd0; bndcl %2, %%bnd0"
                          : : "r"((unsigned long) obj), "r"(32UL), "r"((unsigned long) obj - 1));
        puts("NOT TRAPPED");
    }
    fflush(stdout);
    __asm__ volatile ("bndcl 0(%0), %%bnd0" : : "r"((unsigned long) obj));
    __asm__ volatile ("lea 0(%1), %0" : "=r"(y) : "r"(x));
    printf("lea after bndcl (memory form): %s (%#lx)\n", y == x ? "64-bit" : "TRUNCATED", y);
    fflush(stdout);
    __asm__ volatile ("bndcu 7(%0), %%bnd0" : : "r"((unsigned long) obj));
    __asm__ volatile ("lea 0(%1), %0" : "=r"(y) : "r"(x));
    printf("lea after bndcu (memory form): %s (%#lx)\n", y == x ? "64-bit" : "TRUNCATED", y);
    return 0;
}
