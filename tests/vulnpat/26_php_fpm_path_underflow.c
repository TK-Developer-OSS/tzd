/* 26  Pointer arithmetic that assumes a string is long enough (CWE-124).
   PHP-FPM, CVE-2019-11043 (env_path_info, NVD CVSS 3.0: 9.8).

   Shape: request variables live one after another in an arena (one
   allocation).  The code computes "where the path info starts" as
   path_info + strlen(path_info) - strlen(script_tail), assuming the
   path info is at least as long; when it is empty the pointer lands
   before the string, and the code temporarily stores a terminator there
   (restoring the old byte afterwards), which is inside the previous
   variable.  Here the stored zero cuts the previous variable short.

   GCC (any flags):  the previous variable in the arena is cut short,
                     the program continues.
   TzD:              whether this stops depends on the bounds the arena
                     pointer carries: the write stays inside the arena's
                     one allocation.  Expected to be a gap (an arena
                     handing out sub-ranges of one block).  */
#include "vp.h"

static char *arena_put(char **top, const char *s)
{
    char *p = *top;
    size_t n = strlen(s) + 1;
    memcpy(p, s, n);
    *top += n;
    return p;
}

int main(void)
{
    char *arena = malloc(128), *top = arena;
    char *script, *path_info, *pi;
    size_t pilen, slen = 4;                 /* length of "/x.php" tail */

    vp_begin("26_php_fpm_path_underflow");
    script = arena_put(&top, "SCRIPT=/var/www/index.php");
    path_info = arena_put(&top, "");        /* empty PATH_INFO */

    pilen = strlen(path_info) + (size_t) VP_RT(0);
#if VP_BAD
    pi = path_info + pilen - slen;          /* before the string */
#else
    pi = pilen > slen ? path_info + pilen - slen : path_info;
#endif
    {
        char old = *pi;
        *pi = 0;                            /* temporary terminator */
        (void) old;                         /* (restore omitted) */
    }
    vp_reached();
    if (strcmp(script, "SCRIPT=/var/www/index.php") != 0) {
        vp_effect("corrupted", "the previous request variable was cut short");
        vp_show("script", script, 25);
    } else
        vp_effect("none", "the previous request variable is intact");
    free(arena);
    return vp_end();
}
