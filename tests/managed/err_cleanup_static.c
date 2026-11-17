static void fn(int *p) { (void) p; }
static int x __attribute__((cleanup(fn)));
int main(void) { return x; }
