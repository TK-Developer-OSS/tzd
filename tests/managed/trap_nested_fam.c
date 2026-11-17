/* sa_data (a flexible array in an anonymous union with min[14]) is
   bounded by the sockaddr that holds it: a 15th byte traps.  */
#include <string.h>
struct sa {
  unsigned short family;
  union {
    char min[14];
    struct { struct { } empty; char sa_data[]; };
  };
};
struct req { int x; struct sa addr; int y; };
int main (void)
{
  struct req r;
  struct req *p = &r;
  memcpy (p->addr.sa_data, "abcdefghijklmno", 15);
  return r.y;
}
