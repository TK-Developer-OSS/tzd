/* #pragma tzd lazy_pointer (task #70): the value read while the union holds
   the int member has no bounds: an access through it traps.  */
#pragma tzd lazy_pointer REDIR.filename
#include <stdlib.h>
typedef struct word { int flags; } WORD;
typedef union { int dest; WORD *filename; } REDIR;
struct redirect { REDIR redirectee; };

int main (void)
{
  struct redirect *r = malloc (sizeof *r);
  r->redirectee.dest = 10;
  WORD *p = r->redirectee.filename;
  return p->flags;                      /* traps */
}
