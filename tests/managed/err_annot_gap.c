/* task #79: an annotation must come right before its declaration (here
   a directive stands between them).  */
// @tzd group=g
#define N 4
static int v[N];
int main(void) { return v[0]; }
