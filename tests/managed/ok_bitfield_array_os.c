/* tzd-flags: -Os */
/* Bit-fields in an array of structs written in a loop (the kernel's
   kbd_table): a bit-field has no address (DECL_NONADDRESSABLE_P, as in C).
   Without it -O2 stored whole bytes through &tab[i].k (wrong result)
   and -Os crashed in maybe_canonicalize_mem_ref_addr.  */
struct kbd { unsigned char lockstate, slockstate; unsigned char ledmode:1; unsigned char ledflagstate:4; unsigned char def:4; unsigned char modeflags:5; unsigned char kbdmode:3; };
static struct kbd tab[63];
static struct kbd *kbd = tab;
static int utf8 = 1;
int main (void)
{
  int i;
  for (i = 0; i < 63; i++) {
    tab[i].ledflagstate = 0; tab[i].def = 0; tab[i].ledmode = 0;
    tab[i].lockstate = 0; tab[i].slockstate = 0; tab[i].modeflags = 0;
    tab[i].kbdmode = utf8 ? 3 : 0;
  }
  return kbd[5].kbdmode - 3;
}
