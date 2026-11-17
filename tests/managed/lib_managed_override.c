#include <stdio.h>
#include <stdarg.h>
void error (int status, int errnum, const char *fmt, ...)
{ va_list ap; va_start (ap, fmt); (void) status; (void) errnum; vfprintf (stderr, fmt, ap); va_end (ap); fputc ('\n', stderr); }
