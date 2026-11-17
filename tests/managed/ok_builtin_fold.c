/* GCC builtins fold as in C: __builtin_constant_p on a constant is 1, so
   the kernel's htons (ETH_P_IP) is a constant case label (task #42).  */
typedef unsigned short u16;
static inline u16 fsw(u16 x) { return (u16)((x << 8) | (x >> 8)); }
#define cswab16(x) ((u16)((((u16)(x) & 0x00ffU) << 8) | (((u16)(x) & 0xff00U) >> 8)))
#define swab16(x) ((u16)(__builtin_constant_p((u16)(x)) ? cswab16(x) : fsw(x)))
int f(u16 p) { switch (p) { case swab16(0x0800): return 1; case swab16(0x86DD): return 2; } return 0; }
int main(void) { u16 v = 0x0800; return f(fsw(v)) == 1 && swab16(v) == 0x0008 ? 0 : 1; }
