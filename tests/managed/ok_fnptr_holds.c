/* #pragma tzd fnptr_holds (task #70): readline's KEYMAP_ENTRY.function
   holds a function, a child keymap or a macro string.  Stored data
   pointers keep their bounds and come back as their own type (or as a
   char view); member-to-member copies keep the record; a static table
   entry pointing at a static keymap is registered at startup.  */
#pragma tzd fnptr_holds _keymap_entry.function _keymap_entry char
#include <stdlib.h>
#include <string.h>
typedef int rl_command_func_t (int, int);
typedef struct _keymap_entry { char type; rl_command_func_t *function; } KEYMAP_ENTRY;
typedef KEYMAP_ENTRY *Keymap;
#define ISFUNC 0
#define ISKMAP 1
#define ISMACR 2
#define FUNCTION_TO_KEYMAP(map, key) (Keymap)((map)[key].function)
#define KEYMAP_TO_FUNCTION(data) (rl_command_func_t *)(data)

static int insert (int a, int b) { return a + b; }
static KEYMAP_ENTRY meta_map[4] = { { ISFUNC, insert } };
static KEYMAP_ENTRY root_map[4] = {
  { ISFUNC, insert },
  { ISKMAP, (rl_command_func_t *) meta_map },
};

int main (void)
{
  Keymap m = calloc (4, sizeof (KEYMAP_ENTRY));
  Keymap child = calloc (4, sizeof (KEYMAP_ENTRY));
  child[3].type = ISFUNC;
  child[3].function = insert;
  m[0].type = ISKMAP;
  m[0].function = KEYMAP_TO_FUNCTION (child);
  char *macro = malloc (4);
  strcpy (macro, "ls\n");
  m[1].type = ISMACR;
  m[1].function = KEYMAP_TO_FUNCTION (macro);
  m[2].function = m[0].function;              /* copy keeps the record */
  Keymap k = FUNCTION_TO_KEYMAP (m, 2);
  if (k[3].function (2, 3) != 5)
    return 1;
  if (((char *) m[1].function)[1] != 's')
    return 2;
  k = FUNCTION_TO_KEYMAP (root_map, 1);      /* static registration */
  if (k[0].function (1, 1) != 2)
    return 3;
  free ((char *) m[1].function);              /* char view of any */
  m[1].function = insert;                     /* a function: no record */
  if (m[1].function (1, 2) != 3)
    return 4;
  free (child);
  free (m);
  return 0;
}
