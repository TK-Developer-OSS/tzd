/* `extern T x;` after x's definition still refers to that definition
   (the kernel's EXPORT_SYMBOL repeats the declaration).  */
unsigned long long counter = 41;
extern typeof(counter) counter;

int main(void)
{
    counter++;
    return counter == 42 ? 0 : 1;
}
