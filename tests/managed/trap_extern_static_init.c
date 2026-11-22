/* The bounds of &vals[1] in a static initializer are the whole extern
   array (size read from __tzd_size.vals at startup): vals[3] traps.  */
extern const int vals[];
static const int *p = &vals[1];
const int vals[] = { 10, 20, 30 };
int main(void) { return p[2]; }
