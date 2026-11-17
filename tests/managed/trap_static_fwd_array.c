/* A static array declared before its size: its bounds are the size it
   is defined with later in the unit (states + 3 is one past the end).  */
static struct step { int n; } states[];
static struct step *get (int s) { return states + s; }
static struct step states[] = { { 1 }, { 2 }, { 3 } };
int main (void) { return get (3)->n; }
