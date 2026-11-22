/* An automatic pointer without an initializer is the null pointer: its
   bounds are not whatever the stack held before.  A previous frame leaves
   a valid-looking {addr, base, size} in the same stack bytes.  */
int buf[4];
__attribute__((noinline)) int fill(void)
{
    int *q = buf;
    __asm__ volatile ("" : : "r"(&q) : "memory");
    return q[0];
}
__attribute__((noinline)) int use(void)
{
    int *p;
    __asm__ volatile ("" : : : "memory");
    return *p;
}
int main(void)
{
    fill();
    return use();
}
