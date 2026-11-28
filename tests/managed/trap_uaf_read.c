/* A read through a pointer to a freed heap block traps (task #8,
   CWE-416), also while its page still holds live neighbours.  */
#include <stdlib.h>
struct s { int a, b; };
int main(void)
{
    struct s *keep = malloc(sizeof *keep);
    struct s *other = malloc(sizeof *other);    /* same page, stays live */
    keep->a = 1;
    other->a = 2;
    free(keep);
    return keep->a + other->a;
}
