/* A pointer local of a statement expression is wiped when the statement
   expression ends (task #31): reading it through a saved address traps
   instead of yielding the stale bounds.  */
int main(void)
{
    char buf[4] = "abc";
    char **pp = 0;
    int v = ({ char *p = buf; pp = &p; 7; });
    if (v != 7)
        return 1;
    return **pp;          /* p no longer exists: trap */
}
