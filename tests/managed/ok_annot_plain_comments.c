/* task #79: comments that are not annotations are ignored, also ones that
   mention @tzd in passing and ones in directives.  */
#define N 4 /* see @tzd notes */
// the word @tzdx is not an annotation
static int v[N];   // a trailing comment
int main(void) { return v[0]; }
