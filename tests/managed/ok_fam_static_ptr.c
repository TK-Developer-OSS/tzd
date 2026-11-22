/* A static struct whose flexible array member is initialized (the kernel's
   init_struct_pid.numbers): the elements are inside its bounds.  */
struct upid { int nr; void *ns; };
struct pid { int count; unsigned level; struct upid numbers[]; };
static int ns0;
struct pid init_struct_pid = { .count = 1, .level = 0, .numbers = { { .nr = 0, .ns = &ns0 } } };
struct task { struct pid *thread_pid; };
struct task init_task = { .thread_pid = &init_struct_pid };
int main(void)
{
	struct pid *p = init_task.thread_pid;
	return p->numbers[p->level].ns == &ns0 ? 0 : 1;
}
