/* void * arithmetic keeps the bounds: stepping past the object and
   reading through it traps.  */
int main(void)
{
    char buf[4] = "abc";
    void *v = buf;
    v += 4;
    return *(char *) v;     /* trap */
}
