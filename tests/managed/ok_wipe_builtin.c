/* __tzd_wipe destroys a range in two stages (zero, then random) and drops
   the bounds entries of the pointers stored there (TK 2026-11-07).  */
static char *keep;
int main (void)
{
  char buf[64];
  char *slot[1];
  int i, same = 0;
  for (i = 0; i < 64; i++)
    buf[i] = 'S';
  slot[0] = buf;
  keep = buf;
  __tzd_wipe (buf, 64);
  for (i = 0; i < 64; i++)
    same += buf[i] == 'S';
  if (same > 8)           /* random bytes: about 64/256 expected */
    return 1;
  __tzd_wipe ((char *) slot, sizeof slot);
  return keep == buf ? 0 : 2;
}
