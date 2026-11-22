/* Run time: a pointer slot reached at an address off an 8-byte boundary
   traps in the bounds table (the compile-time check covers declared
   layouts; this reaches the slot through a byte offset).  */
struct q { char *s; };
int main(void)
{
  char c = 'x';
  struct q buf[3];
  struct q *p = (struct q *) ((char *) buf + 1);
  p->s = &c;
  return *p->s;
}
