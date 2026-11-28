/* task #79: a store that names a grouped variable, in a function that is
   not one of its writers, is a compile error.  */
// @tzd group=config writer=load_config
static int debug_level;

static void load_config(void) { debug_level = 1; }

int main(void)
{
    load_config();
    debug_level = 3;
    return 0;
}
