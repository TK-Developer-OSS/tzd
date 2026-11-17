/* Kernel idioms: _Static_assert (file, block and struct scope), GNU void
   pointer arithmetic (1-byte steps), "::" in asm (task #42).  */
_Static_assert(sizeof(long) == 8, "LP64");
struct s { int a; _Static_assert(sizeof(int) == 4, "int"); int b; };
#define POISON ((void *) 0x100 + 0x22)

int main(void)
{
    char buf[8] = "abcdefg";
    void *v = buf;
    void *w = v + 3;
    int x = 1;
    _Static_assert(sizeof(struct s) == 8, "two ints");
    if (*(char *) w != 'd' || (char *) w - buf != 3 || w - v != 3) return 1;
    v++;
    v += 2;
    if (*(char *) v != 'd') return 2;
    if ((unsigned long) POISON != 0x122) return 3;
    __asm__ volatile("incl %0" : "+m"(x) :: "memory");
    return x - 2;
}
