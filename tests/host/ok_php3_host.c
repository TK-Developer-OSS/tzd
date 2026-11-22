/* Host functions and objects PHP 3 uses (task #54): its own
   `extern char **environ;` (no system header declares it here), strtok,
   tempnam (freed by libc2d's free), setlocale, popen, and malloc taken as
   a function pointer (pcre's pcre_malloc = malloc).  */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

extern char **environ;
void *(*my_malloc) (size_t) = malloc;
void (*my_free) (void *) = free;

int main (void)
{
  int n = 0;
  for (char **e = environ; e && *e; e++)
    if (strchr (*e, '='))
      n++;
  if (n == 0)
    return 1;
  char buf[] = "a,b,,c";
  int k = 0;
  for (char *t = strtok (buf, ","); t; t = strtok (NULL, ","))
    k++;
  if (k != 3)
    return 2;
  char *tn = tempnam (NULL, "tzd");
  if (!tn || strlen (tn) < 4)
    return 3;
  free (tn);
  if (!setlocale (LC_ALL, "C"))
    return 4;
  FILE *f = popen ("echo hi", "r");
  char line[16];
  if (!f || !fgets (line, sizeof line, f) || strcmp (line, "hi\n") != 0)
    return 5;
  pclose (f);
  char *m = my_malloc (8);
  memset (m, 0, 8);
  my_free (m);
  return 0;
}
