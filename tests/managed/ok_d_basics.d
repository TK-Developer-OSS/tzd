#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
static bool empty(string s) { return s == null || s[0] == 0; }
int main(void) {
  bool b = true;
  string n = null;
  int *ip = null;
  if (!b || false) return 10;
  if (!empty(n) || !empty("") || empty("x")) return 11;
  if (ip != null) return 12;
  if (sizeof(bool) != 1) return 13;
  char *m = malloc(8);
  strcpy(m, "a1 Z");
  if (!isalpha(m[0]) || !isdigit(m[1]) || !isspace(m[2]) || !isupper(m[3])) return 14;
  if (atoi("  -42") != -42 || abs(-3) != 3) return 15;
  uint8_t u = 255; int64_t w = INT64_MAX;
  if (u != UINT8_MAX || w != INT64_MAX || sizeof(intptr_t) != 8) return 16;
  free(m);
  puts("ok");
  return 0;
}
