/* A host function returning a pointer without a bounds rule is a compile
   error (task #1): lfind is not in host_ptr_rules.  */
#include <search.h>
static int cmp(const void *a, const void *b) { return *(const int *)a != *(const int *)b; }
int main(void) {
  int key = 3, arr[4] = {1, 2, 3, 4};
  size_t n = 4;
  int *r = lfind(&key, arr, &n, sizeof(int), cmp);
  return r != 0;
}
