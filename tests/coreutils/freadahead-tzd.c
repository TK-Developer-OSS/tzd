/* freadahead for the TzD build of coreutils (task #71): how many bytes of
   input FP has read from its file and not yet delivered.  gnulib's
   lib/freadahead.c reads the fields of glibc's FILE; a FILE * is an opaque
   handle under TzD (no bytes reachable), so that code stops at its first
   access.  The same number comes from public calls: the stream's logical
   position (ftello) is behind the file descriptor's offset by exactly the
   read-ahead.  Only a stream that can be repositioned has one worth
   knowing (close_stdin repositions stdin with it); an output stream, a
   pipe or a terminal has none.  Linked ahead of libcoreutils.a, so its
   freadahead.o is not used.  */
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

size_t
freadahead (FILE *fp)
{
  int fd = fileno (fp);
  if (fd < 0)
    return 0;
  off_t logical = ftello (fp);
  if (logical < 0)
    return 0;
  off_t physical = lseek (fd, 0, SEEK_CUR);
  if (physical < 0 || physical <= logical)
    return 0;
  return (size_t) (physical - logical);
}
