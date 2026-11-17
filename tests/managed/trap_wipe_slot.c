/* A pointer stored in wiped memory loses its bounds entry: reading it
   back traps.  */
int main (void)
{
  char c = 'x';
  char *slot[1];
  slot[0] = &c;
  __tzd_wipe ((char *) slot, sizeof slot);
  return *slot[0];
}
