/* task #79 spec 7: an integer made an object pointer is a real address;
   outside kernel mode it is a compile error.  */
int main (void)
{
    volatile unsigned *reg = (volatile unsigned *) 0xFEE00000UL;
    return (int) *reg;
}
