/* Without #pragma tzd lazy_pointer, reading a union's pointer member while
   it holds the int member traps at the read (the default stays strict).  */
#include <stdlib.h>
typedef struct word { int flags; } WORD;
typedef union { int dest; WORD *filename; } REDIR;
struct redirect { REDIR redirectee; };

int main (void)
{
  struct redirect *r = malloc (sizeof *r);
  r->redirectee.dest = 10;
  WORD *p = r->redirectee.filename;     /* traps */
  return p != 0;
}
