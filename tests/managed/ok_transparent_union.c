/* transparent_union (the kernel's release_pages_arg) and (void *) fn
   (cpuhp_setup_state_multi): task #42.  */
struct page { int id; };
struct folio { int id; long pad; };
typedef union {
    struct page **pages;
    struct folio **folios;
} pages_arg __attribute__((__transparent_union__));

static int first_id(pages_arg a, int folio) { return folio ? a.folios[0]->id : a.pages[0]->id; }
static int hook(int x) { return x + 1; }

int main(void)
{
    struct page p = { 7 }, *pp[1] = { &p };
    struct folio f = { 9, 0 }, *fp[1] = { &f };
    void *code = (void *) hook;
    int (*back)(int) = (int (*)(int)) code;
    if (first_id(pp, 0) != 7 || first_id(fp, 1) != 9) return 1;
    if (back(1) != 2) return 2;
    return 0;
}
