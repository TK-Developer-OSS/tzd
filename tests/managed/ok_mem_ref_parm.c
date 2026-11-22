/* tzd-flags: -Os */
/* A constant-offset access into a struct parameter (addrconf.c's
   ipv6_reserved_interfaceid): the MEM_REF is based on the declaration
   itself (GCC's tree_could_trap_p crashed on &a.u + C).  */
struct in6 { union { unsigned char a8[16]; unsigned int a32[4]; } u; };
static _Bool reserved (struct in6 a)
{
  if ((a.u.a32[2] | a.u.a32[3]) == 0) return 1;
  if (a.u.a32[2] == __builtin_bswap32 (0x02005eff) && ((a.u.a32[3] & 0xfe) == 0xfe)) return 1;
  return 0;
}
int main (void) { struct in6 x = { { { 0 } } }; x.u.a32[2] = __builtin_bswap32 (0x02005eff); x.u.a32[3] = 0xff; return !reserved (x); }
