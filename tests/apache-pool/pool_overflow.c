/* Task #13 demo: an overflow from one pool object into the next one.
   Apache's pool (main/alloc.c) hands out consecutive parts of one block.
   Plain C: writing 32 bytes into a 16-byte ap_palloc object silently
   overwrites the neighbor.  Managed C with tests/apache-patches/
   pool-narrow.patch: each part has exactly its own bounds, so the write
   traps at the first byte past it.  Built against the real alloc.o with
   stubs for the few things it calls.  */
#include "httpd.h"
#include "http_log.h"
#include "multithread.h"
#include "buff.h"
#include <stdio.h>
#include <string.h>

void *ap_dummy_mutex = 0;
API_EXPORT(void) ap_block_alarms(void) {}
API_EXPORT(void) ap_unblock_alarms(void) {}
API_EXPORT_NONSTD(void) ap_log_error(const char *file, int line, int level,
                                     const server_rec *s, const char *fmt, ...)
{
    (void) file; (void) line; (void) level; (void) s; (void) fmt;
}
int ap_slack(int fd, int line) { (void) line; return fd; }
Sigfunc *ap_signal(int signo, Sigfunc *func) { (void) signo; return func; }
API_EXPORT(BUFF *) ap_bcreate(pool *p, int flags) { (void) p; (void) flags; return 0; }
API_EXPORT(void) ap_bpushfd(BUFF *fb, int fd_in, int fd_out) { (void) fb; (void) fd_in; (void) fd_out; }

int main(int argc, char **argv)
{
    pool *p = ap_init_alloc();
    char *a = ap_palloc(p, 16);
    char *b = ap_pstrdup(p, "neighbor");
    int n = argc > 1 ? atoi(argv[1]) : 16;
    int i;
    (void) argv;
    for (i = 0; i < n; i++)          /* n > 16 overruns a */
        a[i] = 'A';
    printf("b=%.8s\n", b);
    return strcmp(b, "neighbor") != 0;
}
