/* A static struct whose flexible array member is initialized (GNU C, the
   kernel's init_mm.cpu_bitmap = CPU_BITS_NONE): the variable's size and
   bounds include the elements.  */
struct mm { int users; unsigned long bits[]; };
struct mm init_mm = { .users = 2, .bits = { [0 ... 1] = 0UL } };
struct mm m2 = { 1, { 5, 6 } };
int main (void)
{
  unsigned long *p = m2.bits;
  return init_mm.users + (int) p[1] + (int) init_mm.bits[1] - 8;
}
