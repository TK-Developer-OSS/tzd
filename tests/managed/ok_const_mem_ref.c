/* tzd-flags: -Os -mno-sse */
/* &arr[1].map[0] with arr static: the bounds check is statically true and
   the address is a constant; the dereference is MEM_REF[&arr, C] (an
   INDIRECT_REF of &arr + C crashed GCC's constant pool hash, with the
   kernel's -mno-sse: topology.c's apic_maps[dom].map).  */
struct map { unsigned long map[64]; };
static struct map maps[4];
struct ref { unsigned long *p; int n; };
static int weight (struct ref r) { return (int) r.p[1] + r.n; }
int main (void)
{
  maps[1].map[1] = 5;
  struct ref r = { &maps[1].map[0], 2 };
  return weight (r) - 7;
}
