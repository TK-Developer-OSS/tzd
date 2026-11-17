/* An extern void symbol has an address but no bytes: reading through it
   traps.  */
extern const void sym_begin;
const char sym_begin_storage[4] __asm__("sym_begin") = "abc";
int main(void)
{
    const char *p = (const char *) &sym_begin;
    return p[0];            /* trap */
}
