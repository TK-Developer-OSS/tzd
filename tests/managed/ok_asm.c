/* Inline asm, same as C (TK 2026-11-09).  Managed pointers go in as raw
   addresses; a pointer output keeps the bounds it had (kernel idioms:
   OPTIMIZER_HIDE_VAR, RELOC_HIDE, barrier, asm goto).  */
#define barrier() __asm__ __volatile__("" : : : "memory")
#define OPTIMIZER_HIDE_VAR(var) __asm__ ("" : "=r" (var) : "0" (var))

__asm__(".globl tzd_toplevel_sym\n\t.set tzd_toplevel_sym, 42");

static int add(int a, int b)
{
    int r;
    __asm__("leal (%1,%2), %0" : "=r"(r) : "r"(a), "r"(b));
    return r;
}

static int jump(int x)
{
    __asm__ goto("testl %0, %0\n\tjnz %l[nonzero]" : : "r"(x) : "cc" : nonzero);
    return 0;
nonzero:
    return 1;
}

int main(void)
{
    char buf[8] = "abcdefg";
    char *p = buf + 2;
    unsigned long a;
    int v = 5;
    barrier();
    OPTIMIZER_HIDE_VAR(p);
    if (*p != 'c') return 1;
    __asm__("movq %1, %0" : "=r"(a) : "r"(p));
    if (a != (unsigned long) &buf[2]) return 2;
    __asm__("addq $2, %0" : "+r"(p));          /* moved inside: still ok */
    if (*p != 'e') return 3;
    __asm__("incl %0" : "+m"(v));
    if (v != 6 || add(40, 2) != 42 || jump(0) != 0 || jump(3) != 1) return 4;
    return 0;
}
