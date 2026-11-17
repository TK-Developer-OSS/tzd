/* Brace elision still stops at the end of the outer aggregate: one
   element too many is an error.  */
struct p { int x, y; };
struct w { int k; struct p pp; };
int main (void) { struct w w = { 1, 2, 3, 4 }; return w.k; }
