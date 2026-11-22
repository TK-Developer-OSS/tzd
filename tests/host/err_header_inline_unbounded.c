/* Task #24: inside a system header function body, a host pointer whose
   length is unknown is a compile error (no "bounded below only").  The
   line marker makes the next lines a system header.  */
# 1 "fake-sys.h" 1 3
extern char *fake_host_table;
static inline int fake_lookup (int i) { return fake_host_table[i]; }
# 8 "err_header_inline_unbounded.c" 2
int main (void) { return fake_lookup (0); }
