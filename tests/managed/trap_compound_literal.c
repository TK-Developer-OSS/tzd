/* A compound literal array has exactly its own bounds.  */
int main(void)
{
    int *p = (int[]){ 1, 2, 3 };
    return p[3];
}
