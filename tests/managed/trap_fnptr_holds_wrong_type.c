/* #pragma tzd fnptr_holds: a macro string read back as a keymap traps
   (the recorded type is char, not _keymap_entry).  */
#pragma tzd fnptr_holds _keymap_entry.function _keymap_entry char
#include <stdlib.h>
#include <string.h>
typedef int rl_command_func_t (int, int);
typedef struct _keymap_entry { char type; rl_command_func_t *function; } KEYMAP_ENTRY;
typedef KEYMAP_ENTRY *Keymap;
int main (void)
{
  Keymap m = calloc (4, sizeof (KEYMAP_ENTRY));
  char *s = malloc (4);
  strcpy (s, "abc");
  m[0].function = (rl_command_func_t *) s;
  Keymap k = (Keymap) m[0].function;          /* traps */
  return k->type;
}
