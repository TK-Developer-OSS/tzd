/* Helper for ok_fnptr_host_eq.c (a second file: thunks are shared across
   files).  */
#include <unistd.h>
int is_read (ssize_t (*f) (int, void *, size_t))
{
  return f == read;
}
