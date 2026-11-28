/* A pointer field of a malloc'd struct that was never stored is not a
   null pointer: reading it traps (task #8).  The field has a 16-byte
   granule of its own (the written bits are per granule: a field sharing
   a granule with a written one is not seen).  */
#include <stdlib.h>
struct node { int v; char pad[12]; struct node *next; };
int main(void)
{
    struct node *n = malloc(sizeof *n);
    n->v = 1;
    return n->next ? 1 : 0;
}
