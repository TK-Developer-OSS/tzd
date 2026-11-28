/* task #79: an annotation that applies to no declaration is an error.  */
int main(void)
{
    int x = 0;
    // @tzd group=g
    x++;
    return x;
}
