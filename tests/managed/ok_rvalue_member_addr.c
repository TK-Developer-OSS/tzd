/* The address of an array member of a struct rvalue (f().arr, f returns a
   struct by value): the rvalue is materialized in a temporary so taking the
   address is well-formed.  cc1tzd crashed on this in OpenSSH's ML-KEM code.  */
struct Poly { int c[4]; };
struct Tuple { struct Poly fst[2]; struct Poly snd[2]; };
static struct Tuple h (int base) {
  struct Tuple t;
  for (int i=0;i<2;i++) for (int j=0;j<4;j++){ t.fst[i].c[j]=base+i*10+j; t.snd[i].c[j]=0; }
  return t;
}
int main (void) {
  struct Poly x[2];
  __builtin_memcpy (x, h (100).fst, 2 * sizeof (struct Poly));
  return (x[0].c[0]==100 && x[1].c[3]==113) ? 0 : 1;
}
