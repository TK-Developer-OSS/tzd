/* &&label is a code address with no bytes behind it: reading through it
   traps (as (void *) fn).  */
int main (void)
{
  const unsigned char *p = &&here;
  return p[0];
here:
  return 0;
}
