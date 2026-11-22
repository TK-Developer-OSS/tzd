/* A cast to a union that holds pointers is not supported (its value
   would need its bounds entries): a compile error, not a guess.  */
union u { long v; char *p; };
int main (void) { long x = 1; union u a = (union u) x; return (int) a.v; }
