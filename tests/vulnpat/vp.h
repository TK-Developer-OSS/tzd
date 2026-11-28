/* vp.h: shared harness of the vulnerability-pattern suite (tests/vulnpat).

   Plain C: every program builds unchanged with GCC and with TzD.  Each one
   models one bug class the way it appears in real code, and the faulty
   statement always executes: no input, no race, no timing.  Where the GCC
   build shows a visible effect (a flag rewritten, a neighbour's bytes read,
   two objects confused), the layout that produces it is fixed by the ABI or
   by glibc's allocator, so it is the same on every run.

   Protocol on stdout (one line each, in order):
     BEGIN <id> bad|fixed
     REACHED                  the faulty statement ran and nothing stopped it
     EFFECT <kind> <detail>   what the flaw did; kind is one of
                              none, corrupted, leaked, confused
     END
   A program that is stopped prints only BEGIN (or BEGIN REACHED when the
   stop comes at a later use).  TzD stops with exit status 134 and a
   "tzd:" message on stderr.

   Build with -DVP_FIXED for the corrected program.  It must reach END
   under every compiler: a stop there is a false positive.  */
#ifndef VP_H
#define VP_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VP_FIXED
# define VP_BAD 0
#else
# define VP_BAD 1
#endif

/* A run-time zero: the optimizer cannot fold values that go through it,
   just as it cannot fold real input.  */
static volatile int vp_zero;
#define VP_RT(x) ((x) + vp_zero)

__attribute__((unused)) static void vp_begin(const char *id)
{
#ifndef __TZD__
    /* glibc buffers a piped stdout; unbuffered, the markers survive a
       crash.  (libc2d writes each call through at once.)  */
    setvbuf(stdout, NULL, _IONBF, 0);
#endif
    printf("BEGIN %s %s\n", id, VP_BAD ? "bad" : "fixed");
}

__attribute__((unused)) static void vp_reached(void)
{
    printf("REACHED\n");
}

__attribute__((unused)) static void vp_effect(const char *kind, const char *detail)
{
    printf("EFFECT %s %s\n", kind, detail);
}

/* Print n bytes after a label: printable ASCII as is, anything else '.'.  */
__attribute__((unused)) static void vp_show(const char *label, const char *p, int n)
{
    int i;
    printf("  %s: \"", label);
    for (i = 0; i < n; i++) {
        char c = p[i];
        putchar(c >= 32 && c < 127 ? c : '.');
    }
    printf("\"\n");
}

__attribute__((unused)) static int vp_end(void)
{
    printf("END\n");
    return 0;
}

#endif
