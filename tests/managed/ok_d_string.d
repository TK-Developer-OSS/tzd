#include <string.h>
#include <stdio.h>
static int count(string s) { int n = 0; while (s[n]) n++; return n; }
static string pick(int i) { return i ? "yes" : "no"; }
struct rec { string name; int v; };
int main(void) {
  string a = "hello";
  char *b = a;           /* string -> char* */
  string c = b;          /* char* -> string */
  char buf[16];
  string d = buf;        /* array decays to string */
  struct rec r;
  r.name = a;
  strcpy(buf, a);
  if (count(a) != 5 || strlen(c) != 5 || strcmp(d, "hello")) return 1;
  if (sizeof(string) != sizeof(char *)) return 2;
  if (sizeof(struct rec) != 16) return 3;
  if (strcmp(pick(1), "yes") || strcmp(r.name, "hello")) return 4;
  puts(a);
  return 0;
}
