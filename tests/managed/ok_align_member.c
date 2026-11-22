/* #pragma tzd align_member TYPE.FIELD N (task #70): bash's unwind-protect
   saves a variable's bytes into SAVED_VAR.desired_setting (offset 28 of
   its UNWIND_ELT) and copies them back later.  Aligned to 8, the saved
   copy of a pointer keeps its bounds and the restored pointer loads.  */
#pragma tzd align_member SAVED_VAR.desired_setting 8
#include <stdlib.h>
#include <string.h>
typedef struct {
  char *variable;
  int size;
  char desired_setting[1];
} SAVED_VAR;
struct head { void *next; void *cleanup; };
typedef struct { struct head h; SAVED_VAR v; } ELT;

static char *gvar;

int main (void)
{
  static char text[] = "hello";
  gvar = text;
  ELT *e = malloc (sizeof (ELT) + sizeof (char *));
  memcpy (e->v.desired_setting, &gvar, sizeof gvar);   /* save */
  gvar = 0;
  memcpy (&gvar, e->v.desired_setting, sizeof gvar);   /* restore */
  free (e);
  return gvar[4] == 'o' ? 0 : 1;
}
