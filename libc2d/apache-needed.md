# libc2d: libc functions Apache 1.3.42 imports (httpd built with GCC 14.4)

Source: `nm -D --undefined-only httpd` (145 symbols). Only the string and file
groups are listed here; process, socket, signal, SysV IPC, user/group and
time-of-day groups are not scoped yet.

Status: `have` = in libc2d now (as tzd_* hand-lowered form), `-` = not yet.

## String / memory / text

| Group | Functions | libc2d |
|---|---|---|
| length, compare, search | strlen strcmp strncmp strcasecmp strncasecmp strchr strrchr strstr memchr memcmp | strlen strcmp strncmp strcasecmp strncasecmp strchr strrchr strstr memchr memcmp |
| copy, fill | strcpy strncpy strncat memcpy memmove memset | strcpy strncpy strncat memcpy memmove memset |
| char class / case | tolower toupper `__ctype_b_loc` (isalpha etc. via ctype table) | tolower toupper (no ctype table yet) |
| number <-> text | atoi atol atof strtod `__isoc99_sscanf` sprintf | - |
| text output | printf puts putchar fprintf fputs fputc | printf, puts, putchar |
| errors | strerror strsignal perror | - |
| sort | qsort | - |
| time text | strftime ctime gmtime localtime | - |
| address text | inet_addr inet_ntoa | - |
| password | crypt | - |
| heap | malloc free realloc | malloc free realloc calloc |

## File / stream

| Group | Functions | libc2d |
|---|---|---|
| fd I/O | open close read write writev fcntl dup2 pipe | write only |
| stdio streams | fopen fdopen freopen fclose fread fwrite fgets fgetc getc ungetc fputc fputs fprintf fflush fseek rewind feof ferror fileno | - |
| metadata | stat fstat lstat unlink umask | - |
| directories | opendir readdir closedir chdir | - |
| mapping | mmap munmap | internal only |

## Notes
- Some names (fprintf, fputs, fputc) appear in both groups; the groups are for planning, not a partition.
- Pointer-taking functions (strcpy, memcpy, fread, fgets, read, stat, ...) are
  the ones where the managed-pointer bounds check matters most (task #14).
- The heavy ones for libc2d are the stdio stream layer (FILE, buffering) and
  sscanf/strftime/qsort. Everything else is small.

## Decisions (task #14)
- Functions outside the string and file groups (about 90) are provided by the host libc for now.
- libc2d's string and ctype functions are written from the C standard's definitions (no code from other libraries).
