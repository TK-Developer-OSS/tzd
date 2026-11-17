/* Without #pragma tzd align_member: the saved copy lands at offset 28,
   not a slot, so the restored pointer has no bounds and its load traps.  */
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
  memcpy (e->v.desired_setting, &gvar, sizeof gvar);
  gvar = 0;
  memcpy (&gvar, e->v.desired_setting, sizeof gvar);
  free (e);
  return gvar[4] == 'o' ? 0 : 1;
}
