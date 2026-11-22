/* tzd-flags: -std=gnu23
   C23 attribute specifiers [[...]] (task #71): read as __attribute__.  */
int f (int n)
{
  int r = 0;
  switch (n)
    {
    case 1:
      r += 1;
      [[fallthrough]];
    case 2:
      r += 2;
      [[__fallthrough__]];
    default:
      r += 4;
    }
  return r;
}
[[maybe_unused]] static int unused_fn (void) { return 0; }
int main (void) { return f (1) == 7 && f (2) == 6 && f (9) == 4 ? 0 : 1; }
