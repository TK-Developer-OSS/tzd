/* #pragma tzd lazy_pointer (task #70): reading the annotated pointer member
   of a union while it holds the int member does not trap (bash reads
   REDIRECTEE.filename this way); a real pointer stored there still loads
   with its bounds.  */
#pragma tzd lazy_pointer REDIR.filename
#include <stdlib.h>
typedef struct word { int flags; } WORD;
typedef union { int dest; WORD *filename; } REDIR;
struct redirect { REDIR redirectee; int kind; };

int main (void)
{
  static WORD w = { 5 };
  struct redirect *r = malloc (sizeof *r);
  r->redirectee.dest = 10;              /* the int member */
  WORD *p = r->redirectee.filename;     /* read anyway: no trap */
  if (p == 0)
    return 1;
  r->redirectee.filename = &w;          /* a real pointer */
  p = r->redirectee.filename;
  if (p->flags != 5)
    return 2;
  free (r);
  return 0;
}
