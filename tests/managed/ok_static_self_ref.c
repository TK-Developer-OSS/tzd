/* Static initializers that take addresses inside the object being
   initialized or another static array, at constant indexes (the kernel's
   nmi_desc[] = { .head = LIST_HEAD_INIT(nmi_desc[0].head) }, cpu_root_attrs
   = { &cpu_attrs[0].attr.attr }): the bounds checks are statically true,
   so the addresses stay constants.  Also 0b binary constants (C23/GNU).  */
struct list { struct list *next, *prev; };
struct desc { int lock; struct list head; };
static struct desc nd[2] = {
  { .lock = 0, .head = { &nd[0].head, &nd[0].head } },
  { .lock = 0, .head = { &(nd[1].head), &(nd[1].head) } },
};
struct attr { const char *name; };
struct dattr { struct attr attr; };
struct cattr { struct dattr attr; int x; };
static struct cattr ca[] = { { { { "a" } }, 1 }, { { { "b" } }, 2 } };
static struct attr *roots[] = { &ca[0].attr.attr, &ca[1].attr.attr, 0 };
int main (void) { switch (0b101) { case 0b101: break; default: return 9; } return (nd[1].head.next != &nd[1].head) + (roots[1]->name[0] != 'b') + (0B11 != 3); }
