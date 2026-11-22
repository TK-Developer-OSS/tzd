/* A register asm output into a fresh pointer gives it an address only;
   the pointer has no bounds and dereferencing it traps.  */
int x = 7;
int main(void)
{
    int *p;
    __asm__ ("lea %1, %0" : "=r"(p) : "m"(x));
    return *p;
}
