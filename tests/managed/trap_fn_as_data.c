/* (void *) fn carries the code address but no bytes: reading code through
   it traps.  */
static int hook(int x) { return x + 1; }
int main(void)
{
    const unsigned char *p = (const unsigned char *) (void *) hook;
    return p[0];            /* trap */
}
