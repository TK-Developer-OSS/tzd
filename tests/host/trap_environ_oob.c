/* A string from the host's environ (declared by the program itself) has
   the host rule's bounds: reading past its NUL traps.  */
#include <string.h>
extern char **environ;
int main (void)
{
  char *s = environ[0];
  size_t n = strlen (s);
  return s[n + 1];
}
