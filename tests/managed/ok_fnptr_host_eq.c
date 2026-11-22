/* A host function taken as a Managed C function pointer compares equal to
   itself, in the same file and across files (task #70: OpenSSH's
   atomicio6 picks POLLIN with `f == read`; when this was false scp -t
   polled its stdin for POLLOUT and hung).
   tzd-flags: --host lib_fnptr_other.c */
#include <unistd.h>
int is_read (ssize_t (*f) (int, void *, size_t));
#define vwrite (ssize_t (*) (int, void *, size_t)) write
int main (void)
{
  ssize_t (*g) (int, void *, size_t) = read;
  if (!(g == read) || g != read)
    return 1;
  if (!is_read (read) || !is_read (g))
    return 2;
  if (is_read (vwrite))
    return 3;
  return 0;
}
