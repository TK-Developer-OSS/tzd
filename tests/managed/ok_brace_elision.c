/* Brace elision (C 6.7.9p20): positional initializers fill nested
   aggregates without braces ({0} for a struct whose first member is a
   struct, the kernel's mkhi_gfx_mem_ready req = {0}); an expression of
   the member's own type initializes it whole.  */
struct p { int x, y; };
struct q { struct p a[2]; char s[4]; int *ptr; struct p last; };
static int g = 7;
static struct q sq = { 1, 2, 3, 4, "abc", &g, 5 };
struct w { int k; struct p pp; };
int main (void)
{
  struct q lq = { 1, 2, 3, 4, "xy", &g, { 9, 10 } };
  struct p pv = { 11, 12 };
  struct w w1 = { 1, pv };
  struct w w2 = { 1, 2, 3 };
  struct p arr[] = { 1, 2, 3, 4, 5 };
  if (sq.a[1].y != 4 || sq.s[2] != 'c' || *sq.ptr != 7 || sq.last.x != 5 || sq.last.y != 0) return 1;
  if (lq.a[0].y != 2 || lq.last.y != 10 || *lq.ptr != 7) return 2;
  if (w1.pp.y != 12 || w2.pp.y != 3) return 3;
  if (sizeof arr / sizeof arr[0] != 3 || arr[2].x != 5 || arr[2].y != 0) return 4;
  return 0;
}
