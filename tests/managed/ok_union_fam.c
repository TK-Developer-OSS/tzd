/* Flexible arrays in a union that is the last member (the kernel's
   ipv6_rpl_sr_hdr.segments: DECLARE_FLEX_ARRAY members): through a
   pointer they reach the end of the allocation.  */
#include <stdlib.h>
struct addr { unsigned char b[16]; };
struct hdr {
  unsigned char type, len;
  union {
    struct { struct { } e1; struct addr addr[]; };
    struct { struct { } e2; unsigned char data[]; };
  } segments;
};
int main (void)
{
  struct hdr *h = malloc (sizeof *h + 2 * sizeof (struct addr));
  h->segments.addr[1].b[15] = 7;
  unsigned char *d = &h->segments.data[31];
  int ok = *d == 7;
  free (h);
  return !ok;
}
