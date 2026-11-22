/* An asm that moves a pointer far outside its object gives it a new
   address but not new bounds: the access traps (an asm cannot forge
   bounds).  */
int main(void)
{
    char buf[8] = "abcdefg";
    char *p = buf;
    __asm__("addq $4096, %0" : "+r"(p));
    return *p;                          /* trap */
}
