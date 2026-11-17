// SPDX-License-Identifier: MIT
/* Register clearing at scope exit (TK 2026-11-07, trial; whether to keep
   it is task #39).  cc1tzd zeroes the general registers inline after the
   wipes of a block; this clears the vector registers, whose width depends
   on the CPU: xmm0-15 always (SSE2), ymm uppers with AVX (vzeroall), and
   zmm16-31 / k1-k7 with AVX-512 enabled by the OS.  */

static int tzd_vlevel = -1;           /* 0 SSE2, 1 AVX, 2 AVX-512 */

static void cpuid(unsigned leaf, unsigned sub, unsigned r[4])
{
    __asm__ volatile ("cpuid"
                      : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
                      : "a"(leaf), "c"(sub));
}

static int vlevel(void)
{
    unsigned r[4], lo, hi;
    int lvl = 0;
    cpuid(0, 0, r);
    unsigned max = r[0];
    cpuid(1, 0, r);
    if ((r[2] & (1u << 27)) && (r[2] & (1u << 28))) {   /* OSXSAVE, AVX */
        __asm__ volatile ("xgetbv" : "=a"(lo), "=d"(hi) : "c"(0));
        if ((lo & 6) == 6) {
            lvl = 1;
            if (max >= 7) {
                cpuid(7, 0, r);
                if ((r[1] & (1u << 16)) && (lo & 0xe0) == 0xe0)
                    lvl = 2;
            }
        }
    }
    return lvl;
}

void tzd_vregs_clear(void)
{
    if (tzd_vlevel < 0)
        tzd_vlevel = vlevel();
    if (tzd_vlevel >= 1)
        __asm__ volatile ("vzeroall" ::: "memory");
    else
        __asm__ volatile (
            "pxor %%xmm0,%%xmm0\n\tpxor %%xmm1,%%xmm1\n\t"
            "pxor %%xmm2,%%xmm2\n\tpxor %%xmm3,%%xmm3\n\t"
            "pxor %%xmm4,%%xmm4\n\tpxor %%xmm5,%%xmm5\n\t"
            "pxor %%xmm6,%%xmm6\n\tpxor %%xmm7,%%xmm7\n\t"
            "pxor %%xmm8,%%xmm8\n\tpxor %%xmm9,%%xmm9\n\t"
            "pxor %%xmm10,%%xmm10\n\tpxor %%xmm11,%%xmm11\n\t"
            "pxor %%xmm12,%%xmm12\n\tpxor %%xmm13,%%xmm13\n\t"
            "pxor %%xmm14,%%xmm14\n\tpxor %%xmm15,%%xmm15"
            ::: "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
                "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13",
                "xmm14", "xmm15");
    if (tzd_vlevel >= 2)
        __asm__ volatile (
            "vpxord %%zmm16,%%zmm16,%%zmm16\n\tvpxord %%zmm17,%%zmm17,%%zmm17\n\t"
            "vpxord %%zmm18,%%zmm18,%%zmm18\n\tvpxord %%zmm19,%%zmm19,%%zmm19\n\t"
            "vpxord %%zmm20,%%zmm20,%%zmm20\n\tvpxord %%zmm21,%%zmm21,%%zmm21\n\t"
            "vpxord %%zmm22,%%zmm22,%%zmm22\n\tvpxord %%zmm23,%%zmm23,%%zmm23\n\t"
            "vpxord %%zmm24,%%zmm24,%%zmm24\n\tvpxord %%zmm25,%%zmm25,%%zmm25\n\t"
            "vpxord %%zmm26,%%zmm26,%%zmm26\n\tvpxord %%zmm27,%%zmm27,%%zmm27\n\t"
            "vpxord %%zmm28,%%zmm28,%%zmm28\n\tvpxord %%zmm29,%%zmm29,%%zmm29\n\t"
            "vpxord %%zmm30,%%zmm30,%%zmm30\n\tvpxord %%zmm31,%%zmm31,%%zmm31\n\t"
            "kxorw %%k1,%%k1,%%k1\n\tkxorw %%k2,%%k2,%%k2\n\t"
            "kxorw %%k3,%%k3,%%k3\n\tkxorw %%k4,%%k4,%%k4\n\t"
            "kxorw %%k5,%%k5,%%k5\n\tkxorw %%k6,%%k6,%%k6\n\t"
            "kxorw %%k7,%%k7,%%k7" ::: "memory");
}
