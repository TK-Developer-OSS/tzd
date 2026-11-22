/* {0} with nested aggregates first, and a cast to a union (GNU C,
   ibs.c's (union ibs_fetch_ctl) config).  */
struct hdr { unsigned char group; unsigned char cmd; };
struct req { struct hdr h; unsigned int flags; };
union ctl { unsigned long val; struct { unsigned a:8; } f; };
struct rt { union { struct { int x; } d; } u; int *p; };
int main (void)
{
  struct req r = {0};
  struct rt t = { 0 };
  unsigned long config = 5;
  union ctl c = (union ctl) config;
  return r.h.group + r.flags + t.u.d.x + (t.p != 0) + (int) c.val - 5;
}
