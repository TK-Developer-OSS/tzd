/* #pragma tzd fnptr_holds: a function read back as a data pointer traps
   (no data pointer was recorded).  */
#pragma tzd fnptr_holds _keymap_entry.function _keymap_entry char
#include <stdlib.h>
typedef int rl_command_func_t (int, int);
typedef struct _keymap_entry { char type; rl_command_func_t *function; } KEYMAP_ENTRY;
typedef KEYMAP_ENTRY *Keymap;
static int insert (int a, int b) { return a + b; }
int main (void)
{
  Keymap m = calloc (4, sizeof (KEYMAP_ENTRY));
  m[0].function = insert;
  Keymap k = (Keymap) m[0].function;          /* traps */
  return k->type;
}
