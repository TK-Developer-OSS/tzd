/* task #26/#61: `#pragma tzd returns_twice` accepts the attribute on a
   declaration (it reaches TzD only via pthread.h in OpenSSL, which never
   calls the setjmp-family function).  The declaration alone exercises the
   attribute; without the pragma it is a compile error (err_returns_twice.c).  */
#pragma tzd returns_twice
extern int __sj (void *env, int save) __attribute__((returns_twice));
int main (void) { return 0; }
