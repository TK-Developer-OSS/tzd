/* Flexible arrays reached through a pointer (task #42, kernel):
   - a struct ending in a flexible array as the last member of another
     (pci_cap_saved_state.cap.data[]): bounds of the whole allocation;
   - a flexible array in an anonymous struct inside an anonymous union
     (sockaddr's DECLARE_FLEX_ARRAY sa_data next to sa_data_min[14]):
     bounds of the named object that holds it.  */
#include <stdlib.h>
#include <string.h>
struct data { unsigned short nr; unsigned int size; unsigned int d[]; };
struct state { void *next; struct data cap; };
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
  struct state *s = malloc (sizeof *s + 4 * sizeof (unsigned int));
  unsigned int *cap = &s->cap.d[0];
  cap[3] = 7;
  struct req r;
  memcpy (r.addr.sa_data, "abcdefghijklmn", 14);
  int ok = s->cap.d[3] == 7 && r.addr.min[13] == 'n';
  free (s);
  return !ok;
}
