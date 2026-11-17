/* The union's flexible arrays end with the allocation: data[32] is one
   byte past it.  */
#include <stdlib.h>
struct hdr {
  unsigned char type, len;
  union {
    struct { struct { } e1; unsigned int w[]; };
    struct { struct { } e2; unsigned char data[]; };
  } segments;
};
int main (void)
{
  struct hdr *h = malloc (sizeof *h + 32);
  return h->segments.data[32];
}
