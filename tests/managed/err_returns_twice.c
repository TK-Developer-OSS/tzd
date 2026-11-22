/* task #26: returns_twice is rejected without an explicit #pragma tzd. */
extern int __sj (void *env, int save) __attribute__((returns_twice));
int main (void) { return 0; }
