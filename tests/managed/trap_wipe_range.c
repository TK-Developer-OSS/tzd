/* __tzd_wipe cannot reach past its pointer's bounds.  */
int main (void)
{
  char a[8], b[8];
  b[0] = 1;
  __tzd_wipe (a, 16);
  return b[0];
}
