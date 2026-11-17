/* Enumerated types as GCC: unsigned int without negatives, int with
   them, 64-bit when a value needs it; no value is truncated (it used to
   be cut to int silently; task #42).  */
enum small { A, B = 5, C };
enum neg { N = -1, P = 1 };
enum big { X = 1UL << 40, Y };
enum huge { H = 0xffffffffffffffffUL };
struct bf { enum small s : 3; enum neg n : 2; };
enum pk { PA, PB = 200 } __attribute__((packed));
enum pkn { QA = -1, QB = 300 } __attribute__((__packed__));

int main(void)
{
    enum small s = (enum small) -1;
    enum neg n = (enum neg) -1;
    struct bf b = { C, N };
    if (C != 6 || sizeof(enum small) != 4 || !(s > 0) || !(n < 0)) return 1;
    if (sizeof(enum big) != 8 || Y != (1UL << 40) + 1 || X <= 0) return 2;
    if (H != 0xffffffffffffffffUL || sizeof(enum huge) != 8) return 3;
    if (b.s != 6 || b.n != -1 || sizeof(A) != 4) return 4;
    if (sizeof(enum pk) != 1 || sizeof(enum pkn) != 2 || (enum pk) -1 < 0) return 5;
    return 0;
}
