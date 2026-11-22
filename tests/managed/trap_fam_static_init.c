/* The bounds of an initialized flexible array member end with its last
   initialized element: bits[2] traps.  */
struct mm { int users; unsigned long bits[]; };
struct mm m2 = { 1, { 5, 6 } };
int main (void) { unsigned long *p = m2.bits; return (int) p[2]; }
