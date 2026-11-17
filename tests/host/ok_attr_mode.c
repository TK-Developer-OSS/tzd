#include <sys/types.h>
#include <stdio.h>
typedef unsigned int u64m __attribute__((__mode__(__DI__)));
struct s { int a; long b __attribute__((unused)); };
static int f(int) __attribute__((const));
static int f(int x) { return x; }
int main(void) {
  printf("%d %d\n", (int)sizeof(register_t), (int)sizeof(u64m));
  return (sizeof(register_t) == 8 && sizeof(u64m) == 8 && f(0) == 0) ? 0 : 1;
}
