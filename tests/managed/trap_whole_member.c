/* #pragma tzd whole_member TYPE.FIELD (task #70): bison's stack relocation
   (YYSTACK_RELOCATE) copies a whole stack to &yyptr->yyss_alloc of an
   array of union yyalloc.  With the pragma the member's address keeps the
   allocation's bounds; without it the copy is out of bounds and traps.  */
#include <stdlib.h>
#include <string.h>
typedef signed char yy_state_t;
typedef union { long l; char *p; } YYSTYPE;
union yyalloc { yy_state_t yyss_alloc; YYSTYPE yyvs_alloc; };
int main (void)
{
  yy_state_t old[200];
  memset (old, 1, sizeof old);
  union yyalloc *yyptr = malloc (400 * sizeof (union yyalloc));
  yy_state_t *yyss = &yyptr->yyss_alloc;
  memcpy (yyss, old, sizeof old);
  int r = yyss[199] == 1 ? 0 : 1;
  free (yyptr);
  return r;
}
