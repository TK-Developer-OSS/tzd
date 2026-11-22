/* aligned and packed give the same layout as GCC's C front end
   (task #26/#42: the Linux kernel uses both; __int128 as well).  */
typedef __signed__ __int128 s128 __attribute__((aligned(16)));
typedef unsigned long u64a __attribute__((aligned(16)));

struct pk { char c; int i; short h; } __attribute__((packed));
struct __attribute__((packed)) pk2 { char c; long l; };
struct al { char c; } __attribute__((aligned(64)));
struct mem { char c; int i __attribute__((aligned(16))); char d; };
struct mix { char c; struct pk p; char *s; };          /* pointer stays aligned */
struct pkm { char c; int i __attribute__((packed)); };
struct big { char c; u64a v; };

static char buf[3] __attribute__((aligned(32)));
static int fn(void) __attribute__((aligned(32)));
static int fn(void) { return 1; }

int main(void)
{
    unsigned __int128 u = ((unsigned __int128) 1 << 100) + 7;
    s128 s = -(s128) 1;
    if (sizeof(struct pk) != 7 || __alignof__(struct pk) != 1) return 1;
    if (sizeof(struct pk2) != 9 || __builtin_offsetof(struct pk2, l) != 1) return 2;
    if (sizeof(struct al) != 64 || __alignof__(struct al) != 64) return 3;
    if (__builtin_offsetof(struct mem, i) != 16 || sizeof(struct mem) != 32) return 4;
    if (__builtin_offsetof(struct mix, s) != 8 || sizeof(struct mix) != 16) return 5;
    if (__builtin_offsetof(struct pkm, i) != 1 || sizeof(struct pkm) != 5) return 6;
    if (__builtin_offsetof(struct big, v) != 16 || sizeof(struct big) != 32) return 7;
    if (((unsigned long) buf & 31) != 0 || ((unsigned long) &fn & 31) != 0) return 8;
    if (sizeof(u) != 16 || (u >> 100) != 1 || (int) (u & 0xff) != 7) return 9;
    if (s >= 0 || __alignof__(s128) != 16) return 10;
    return fn() - 1;
}
