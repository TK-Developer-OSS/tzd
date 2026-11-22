/* Intel MPX for the bounds checks of cc1tzd's -ftzd-mpx mode (TK
   2026-11-17, experimental).

   In that mode a check is three MPX instructions (bndmk, bndcl, bndcu)
   instead of compare and branch.  On a processor without MPX (before
   Skylake, after Comet Lake, AMD), or while MPX is not enabled, those
   instructions do nothing at all -- the program would run unchecked.  So
   every unit compiled with -ftzd-mpx puts tzd_mpx_init into
   .preinit_array: it runs before any constructor and before main, enables
   MPX for the process, and stops the program when that is not possible.

   Enabling: MPX is on in user mode when BNDCFGU.EN is 1.  BNDCFGU is
   part of the XSAVE state (component 4, BNDCSR) and is written with
   XRSTOR; the OS must have enabled the MPX components in XCR0 (bits 3
   and 4).  Linux removed its own MPX support (bound tables, prctl) in
   5.6 but still enables these components, which is all the checks need:
   bndldx/bndstx and the bound tables are not used -- the bounds of
   pointers in memory stay in libc2d's table (shadow.c).

   A violation raises #BR, which Linux delivers as SIGSEGV.  The handler
   installed here looks at BNDSTATUS in the signal frame: a bound
   violation is reported like the other traps (exit status 134); any
   other SIGSEGV goes on to the default action.

   Known gap (the OS, not the checks): Linux resets the extended state
   for a signal handler, so BNDCFGU.EN is 0 while a handler runs and the
   checks in code called from a signal handler do nothing.  */
#include <d/pointer.h>
#include "sys.h"

#define XSAVE_BNDREGS (1UL << 3)
#define XSAVE_BNDCSR  (1UL << 4)

static unsigned char mpx_area[8192] __attribute__((aligned(64)));
static unsigned mpx_csr_off;           /* offset of BNDCSR in the XSAVE area */
static int mpx_done;

static void cpuid(unsigned leaf, unsigned sub, unsigned r[4])
{
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(sub));
}

static long sys4(long n, long a, long b, long c, long d)
{
    long r;
    register long r10 __asm__("r10") = d;
    __asm__ volatile ("syscall" : "=a"(r)
                      : "0"(n), "D"(a), "S"(b), "d"(c), "r"(r10)
                      : "rcx", "r11", "memory");
    return r;
}

static void put(const char *s)
{
    tzd_size_t n = 0;
    while (s[n])
        n++;
    tzd_sys_write(2, tzd_ptr_make((void *) s, n), n);
}

static void put_hex(unsigned long v)
{
    char b[19];
    int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++)
        b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
    b[18] = 0;
    put(b);
}

/* rt_sigreturn, as the restorer the kernel wants from a static program.  */
__asm__ (".text\n"
         "\t.type tzd_mpx_restorer, @function\n"
         "tzd_mpx_restorer:\n"
         "\tmovq $15, %rax\n"
         "\tsyscall\n"
         "\t.size tzd_mpx_restorer, .-tzd_mpx_restorer\n");
extern void tzd_mpx_restorer(void);

struct ksigaction {
    void (*handler)(int, void *, void *);
    unsigned long flags;
    void (*restorer)(void);
    unsigned long mask;
};

/* SIGSEGV.  The interrupted program's XSAVE state is in the signal frame
   (ucontext.uc_mcontext.fpregs); BNDSTATUS there says whether this was a
   bound violation (low two bits 01).  SA_RESETHAND: the default action
   is back in place, so returning repeats the fault and ends the program
   the ordinary way when it was something else.  */
static void mpx_segv(int sig, void *info, void *uc)
{
    unsigned char *fp = *(unsigned char **) ((char *) uc + 224);
    unsigned long rip = *(unsigned long *) ((char *) uc + 168);
    (void) sig; (void) info;
    if (fp && (*(unsigned long *) (fp + 512) & XSAVE_BNDCSR)
        && (*(unsigned long *) (fp + mpx_csr_off + 8) & 3) == 1) {
        put("tzd: out-of-bounds pointer access (MPX bound check, #BR)\n"
            "  at     : ");
        put_hex(rip);
        put("   (addr2line -f -e <program> <address>)\n");
        tzd_sys_exit(134);
    }
}

void tzd_mpx_init(void)
{
    unsigned r[4], lo, hi;
    struct ksigaction sa;
    if (mpx_done)
        return;
    cpuid(0, 0, r);
    if (r[0] < 0xd)
        tzd_trap("tzd: this program checks bounds with Intel MPX (-ftzd-mpx), "
                 "and this processor has no MPX\n");
    cpuid(7, 0, r);
    if (!(r[1] & (1u << 14)))
        tzd_trap("tzd: this program checks bounds with Intel MPX (-ftzd-mpx), "
                 "and this processor has no MPX\n");
    cpuid(1, 0, r);
    if (!(r[2] & (1u << 27)))
        tzd_trap("tzd: MPX bounds checks: the OS does not use XSAVE\n");
    __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
    if ((lo & (XSAVE_BNDREGS | XSAVE_BNDCSR)) != (XSAVE_BNDREGS | XSAVE_BNDCSR))
        tzd_trap("tzd: MPX bounds checks: the OS has not enabled the MPX "
                 "state (XCR0 bits 3 and 4)\n");
    cpuid(0xd, 0, r);
    if (r[1] > sizeof mpx_area)
        tzd_trap("tzd: MPX bounds checks: the XSAVE area is larger than expected\n");
    cpuid(0xd, 4, r);
    mpx_csr_off = r[1];
    if (r[0] < 16 || mpx_csr_off < 576 || mpx_csr_off + 16 > sizeof mpx_area)
        tzd_trap("tzd: MPX bounds checks: unexpected place of BNDCSR in the XSAVE area\n");

    /* The handler first: from the next instructions on a check can trap.  */
    sa.handler = mpx_segv;
    sa.flags = 4 /* SA_SIGINFO */ | 0x04000000 /* SA_RESTORER */
               | 0x80000000UL /* SA_RESETHAND */;
    sa.restorer = tzd_mpx_restorer;
    sa.mask = 0;
    if (sys4(13 /* rt_sigaction */, 11 /* SIGSEGV */, (long) &sa, 0, 8) != 0)
        tzd_trap("tzd: MPX bounds checks: cannot install the SIGSEGV handler\n");

    /* BNDCFGU = EN, BNDSTATUS = 0; only this component is loaded.  */
    *(unsigned long *) (mpx_area + 512) = XSAVE_BNDCSR;       /* XSTATE_BV */
    *(unsigned long *) (mpx_area + 520) = 0;                  /* XCOMP_BV */
    *(unsigned long *) (mpx_area + mpx_csr_off) = 1;
    *(unsigned long *) (mpx_area + mpx_csr_off + 8) = 0;
    __asm__ volatile ("xrstor (%0)" : : "r"(mpx_area), "a"(XSAVE_BNDCSR), "d"(0)
                      : "memory");

    /* Read it back: enabled, or stop.  */
    *(unsigned long *) (mpx_area + mpx_csr_off) = 0;
    *(unsigned long *) (mpx_area + 512) = 0;
    __asm__ volatile ("xsave (%0)" : : "r"(mpx_area), "a"(XSAVE_BNDCSR), "d"(0)
                      : "memory");
    if (!(*(unsigned long *) (mpx_area + 512) & XSAVE_BNDCSR)
        || !(*(unsigned long *) (mpx_area + mpx_csr_off) & 1))
        tzd_trap("tzd: MPX bounds checks: MPX could not be enabled (BNDCFGU.EN)\n");
    mpx_done = 1;
}
