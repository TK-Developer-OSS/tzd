/* ... and an index past the run-time size traps.  */
static volatile int n = 8;

int main (void)
{
    char buf[n];
    buf[n] = 1;
    return buf[0];
}
