#include <stdlib.h>
static int cmp(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
int main(void) {
  int (*f)(const void *, const void *) = cmp;
  int arr[3] = {3, 1, 2};
  void *v = arr;
  int (*h)(const void *, const void *, void *) = (int (*)(const void *, const void *, void *)) f;
  qsort_r(v, 3, sizeof(int), h, 0);
  return 0;
}
