/* An empty struct (GNU C; the kernel's spinlock_t on UP) has nothing to
   read or write: using one behind a pointer with no bounds does not trap
   (kmalloc(0) returns ZERO_SIZE_PTR).  */
struct lock { };
static void lock_init(struct lock *l) { *l = (struct lock){}; }

int main(void)
{
    struct lock *l = (struct lock *)16;
    lock_init(l);
    lock_init(l + 3);
    return 0;
}
