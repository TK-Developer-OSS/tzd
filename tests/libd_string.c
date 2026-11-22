/* Functional checks for the string/memory functions written for libc2d. */
#include <d/stdio.h>
#include <d/stdlib.h>
#include <d/string.h>

#define S(x) tzd_ptr_make((void *) (x), sizeof (x))
static int fails;
#define CHECK(c) do { if (!(c)) { fails++; \
    tzd_printf(S("FAIL line %d\n"), __LINE__); } } while (0)

int main(void) {
    tzd_ptr hello = S("Hello, World");
    tzd_ptr buf = tzd_malloc(32);

    /* memchr / memcmp / memset */
    CHECK(tzd_memchr(hello, 'W', 12).addr == hello.addr + 7);
    CHECK(tzd_memchr(hello, 'z', 12).base == 0);
    CHECK(tzd_memcmp(S("abc"), S("abd"), 3) < 0);
    CHECK(tzd_memcmp(S("abc"), S("abc"), 3) == 0);
    tzd_memset(buf, 'x', 5); tzd_ptr_store8(tzd_ptr_add(buf, 5), 0);
    CHECK(tzd_strcmp(buf, S("xxxxx")) == 0);

    /* strchr / strrchr / strstr */
    CHECK(tzd_strchr(hello, 'o').addr == hello.addr + 4);
    CHECK(tzd_strrchr(hello, 'o').addr == hello.addr + 8);
    CHECK(tzd_strchr(hello, 0).addr == hello.addr + 12);
    CHECK(tzd_strchr(hello, 'q').base == 0);
    CHECK(tzd_strstr(hello, S("World")).addr == hello.addr + 7);
    CHECK(tzd_strstr(hello, S("")).addr == hello.addr);
    CHECK(tzd_strstr(hello, S("Word")).base == 0);

    /* strcmp family */
    CHECK(tzd_strcmp(S("a"), S("b")) < 0);
    CHECK(tzd_strncmp(S("abcX"), S("abcY"), 3) == 0);
    CHECK(tzd_strcasecmp(S("HeLLo"), S("hello")) == 0);
    CHECK(tzd_strncasecmp(S("HELP"), S("hello"), 3) == 0);
    CHECK(tzd_strncasecmp(S("HELP"), S("hello"), 4) != 0);

    /* memmove with overlap, strncpy padding, strncat */
    tzd_strcpy(buf, S("abcdef"));
    tzd_memmove(tzd_ptr_add(buf, 2), buf, 4);            /* abababf? -> ababcd */
    CHECK(tzd_strcmp(buf, S("ababcd")) == 0);
    tzd_memset(buf, 'Z', 10);
    tzd_strncpy(buf, S("hi"), 6);
    CHECK(tzd_ptr_load8(tzd_ptr_add(buf, 1)) == 'i' && tzd_ptr_load8(tzd_ptr_add(buf, 5)) == 0
          && tzd_ptr_load8(tzd_ptr_add(buf, 6)) == 'Z');
    tzd_strcpy(buf, S("ab"));
    tzd_strncat(buf, S("cdefg"), 3);
    CHECK(tzd_strcmp(buf, S("abcde")) == 0);

    /* realloc keeps contents, grows, and the new bounds apply */
    tzd_strcpy(buf, S("keep"));
    {
        tzd_ptr big = tzd_realloc(buf, 100);
        CHECK(tzd_strcmp(big, S("keep")) == 0);
        tzd_ptr_store8(tzd_ptr_add(big, 99), 'e');       /* valid in the new size */
        tzd_free(big);
    }

    if (fails) { tzd_printf(S("%d failure(s)\n"), fails); return 1; }
    tzd_puts(S("libd_string: all ok"));

    /* strchr on an unterminated buffer must trap, not run off the end */
    tzd_ptr_store8(tzd_ptr_add(tzd_malloc(4), 0), 'a');
    {
        tzd_ptr q = tzd_malloc(4);
        tzd_memset(q, 'a', 4);
        tzd_strchr(q, 'z');                            /* no NUL in 4 bytes */
    }
    tzd_puts(S("NOT REACHED"));
    return 0;
}
