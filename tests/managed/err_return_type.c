/* tzd-flags: -Werror=return-type */
/* The flag reaches the compiler: a real missing return is an error.  */
static int f (int x) { if (x) return 0; }
int main (void) { return f (1); }
