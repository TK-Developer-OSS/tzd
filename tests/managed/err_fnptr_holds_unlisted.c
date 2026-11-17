/* #pragma tzd fnptr_holds: storing a pointer to a type the pragma does not
   list is a compile error.  */
#pragma tzd fnptr_holds _keymap_entry.function _keymap_entry
#include <stdlib.h>
typedef int rl_command_func_t (int, int);
typedef struct _keymap_entry { char type; rl_command_func_t *function; } KEYMAP_ENTRY;
int main (void)
{
  KEYMAP_ENTRY *m = calloc (4, sizeof (KEYMAP_ENTRY));
  int *x = calloc (1, sizeof (int));
  m[0].function = (rl_command_func_t *) x;
  return 0;
}
