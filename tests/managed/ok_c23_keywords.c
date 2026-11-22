/* tzd-flags: -std=gnu23
   C23 spellings coreutils 9.11 uses (task #71): nullptr, bool/true/false,
   alignof, static_assert as keywords.  */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
_Static_assert (1, "x");
int main (void) {
  int *p = nullptr;
  bool b = true;
  size_t a = alignof (uint32_t);
  static_assert (alignof (long) == 8);
  return p == nullptr && b && a == 4 ? 0 : 1;
}
