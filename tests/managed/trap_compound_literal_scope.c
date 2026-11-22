/* A compound literal lives until the end of its block; a pointer kept in
   one is wiped with it (task #1 block exit).  */
struct holder { char *p; };
int main(void)
{
    char buf[4] = "abc";
    char **keep;
    {
        struct holder *h = &(struct holder){ buf };
        keep = &h->p;
    }
    return **keep;
}
