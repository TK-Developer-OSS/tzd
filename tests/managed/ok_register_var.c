/* GNU register variables (the kernel's current_stack_pointer, call_on_stack): they name
   a machine register; reading one reads the register, and a block that
   declares one does not wipe it.  */
register unsigned long current_stack_pointer __asm__("rsp");

static unsigned long sp_now(void)
{
    return current_stack_pointer;
}

/* A pointer in a register is a raw address handed to asm (the kernel's
   call_on_stack).  */
static unsigned long raw_addr(int *p)
{
    register void *tos __asm__("r11");
    unsigned long out;
    tos = (void *)p;
    __asm__ volatile ("mov %[tos], %[out]" : [out] "=r"(out) : [tos] "r"(tos));
    return out;
}

int main(void)
{
    int x;
    if (raw_addr(&x) != (unsigned long)&x)
        return 3;
    unsigned long a = sp_now(), b;
    {
        register unsigned long r __asm__("r12") = 5;
        __asm__ volatile ("add $1, %0" : "+r"(r));
        b = r;
    }
    /* rsp is somewhere near a local's address.  */
    if (a == 0 || (a > (unsigned long)&a ? a - (unsigned long)&a : (unsigned long)&a - a) > 4096)
        return 1;
    return b == 6 ? 0 : 2;
}
