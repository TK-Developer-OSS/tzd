/* TzD front end: identifiers, scopes, builtins, and the entry points that
   c-family/ expects a front end to provide.

   This replaces what cc1 gets from gcc/c/c-decl.cc, c-objc-common.cc and
   c-typeck.cc.  Entry points that only C's own parser would reach (OpenMP,
   C-specific folding, etc.) are reported as "not supported" if they are
   ever called.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "target.h"
#include "function.h"
#include "c-family/c-common.h"
#include "tree.h"
#include "stringpool.h"
#include "cgraph.h"
#include "diagnostic.h"
#include "tree-diagnostic.h"
#include "langhooks.h"
#include "attribs.h"
#include "stor-layout.h"
#include "fold-const.h"
#include "convert.h"
#include "debug.h"
#include "c-family/c-pragma.h"
#include "c-family/c-pretty-print.h"
#include "gcc-rich-location.h"
#include "tzd-tree.h"

/* ------------------------------------------------------------------ */
/* Language-specific tree node layout (GC).                            */

struct GTY(()) lang_identifier {
  struct c_common_identifier common_id;
  tree symbol;   /* current ordinary binding (decl), or NULL */
  tree tag;      /* current struct/union/enum tag binding (type), or NULL */
};

union GTY((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE"),
	   chain_next ("CODE_CONTAINS_STRUCT (TREE_CODE (&%h.generic), TS_COMMON) ? ((union lang_tree_node *) TREE_CHAIN (&%h.generic)) : NULL")))
  lang_tree_node {
  union tree_node GTY ((tag ("0"), desc ("tree_node_structure (&%h)")))
    generic;
  struct lang_identifier GTY ((tag ("1"))) identifier;
};

struct GTY(()) lang_type { char dummy; };
struct GTY(()) lang_decl { char dummy; };
struct GTY(()) language_function { int dummy; };

#define ID_SYMBOL(ID) (((struct lang_identifier *) (ID))->symbol)
#define ID_TAG(ID) (((struct lang_identifier *) (ID))->tag)

/* tzd-lang.cc's LANG_HOOKS_IDENTIFIER_SIZE must match.  */
STATIC_ASSERT (sizeof (struct lang_identifier)
	       == sizeof (struct c_common_identifier) + 2 * sizeof (void *));

size_t
tzd_identifier_size (void)
{
  return sizeof (struct lang_identifier);
}

/* ------------------------------------------------------------------ */
/* Scopes.

   scope_log records, per binding, (identifier, previous symbol, previous
   tag); a NULL_TREE triple marks the start of a scope.  scope_decls holds
   the decls bound in each open scope, for BIND_EXPR_VARS.  */

static GTY(()) vec<tree, va_gc> *scope_log;
static GTY(()) vec<tree, va_gc> *scope_decls;
static int scope_depth;

void
tzd_push_scope (void)
{
  vec_safe_push (scope_log, NULL_TREE);
  vec_safe_push (scope_log, NULL_TREE);
  vec_safe_push (scope_log, NULL_TREE);
  vec_safe_push (scope_decls, NULL_TREE);
  scope_depth++;
}

tree
tzd_pop_scope (void)
{
  gcc_assert (scope_depth > 0);
  for (;;)
    {
      tree old_tag = scope_log->pop ();
      tree old_sym = scope_log->pop ();
      tree id = scope_log->pop ();
      if (id == NULL_TREE)
	break;
      ID_SYMBOL (id) = old_sym;
      ID_TAG (id) = old_tag;
    }
  scope_depth--;

  /* Collect this scope's decls in declaration order.  */
  tree head = NULL_TREE;
  for (;;)
    {
      tree d = scope_decls->pop ();
      if (d == NULL_TREE)
	break;
      if (TREE_CODE (d) == VAR_DECL || TREE_CODE (d) == TYPE_DECL)
	{
	  DECL_CHAIN (d) = head;
	  head = d;
	}
    }
  return head;
}

bool
global_bindings_p (void)
{
  return scope_depth <= 1;
}

static void
log_binding (tree id)
{
  vec_safe_push (scope_log, id);
  vec_safe_push (scope_log, ID_SYMBOL (id));
  vec_safe_push (scope_log, ID_TAG (id));
}

tree
pushdecl (tree decl)
{
  tree id = DECL_NAME (decl);
  if (id == NULL_TREE)
    return decl;
  if (scope_depth == 0)
    tzd_push_scope ();
  log_binding (id);
  ID_SYMBOL (id) = decl;
  if (TREE_CODE (decl) != FUNCTION_DECL || DECL_CONTEXT (decl) == NULL_TREE)
    vec_safe_push (scope_decls, decl);
  return decl;
}

tree
tzd_lookup (tree id)
{
  return ID_SYMBOL (id);
}

void
tzd_push_tag (tree id, tree type)
{
  log_binding (id);
  ID_TAG (id) = type;
}

/* Tags are looked up through the scope log so that "current scope only"
   can be answered (needed for "struct x;" redeclaration rules).  */
tree
tzd_lookup_tag (tree id, bool current_scope_only)
{
  if (!current_scope_only)
    return ID_TAG (id);
  for (unsigned i = scope_log->length (); i >= 3; i -= 3)
    {
      tree lid = (*scope_log)[i - 3];
      if (lid == NULL_TREE)
	return NULL_TREE;
      if (lid == id)
	return ID_TAG (id);
    }
  return NULL_TREE;
}

/* ------------------------------------------------------------------ */
/* Entry points used by c-family/ (and by the middle end via hooks).  */

tree
lookup_name (tree id)
{
  tree d = ID_SYMBOL (id);
  return d;
}

tree
identifier_global_value (tree id)
{
  /* Skip back over block-scope bindings.  */
  tree d = ID_SYMBOL (id);
  if (scope_log)
    for (unsigned i = scope_log->length ();
	 i >= 3 && d && DECL_P (d) && DECL_CONTEXT (d)
	 && TREE_CODE (DECL_CONTEXT (d)) == FUNCTION_DECL; i -= 3)
      if ((*scope_log)[i - 3] == id)
	d = (*scope_log)[i - 2];
  return d;
}

tree
identifier_global_tag (tree id)
{
  return ID_TAG (id);
}

bool
names_builtin_p (const char *name)
{
  tree id = get_identifier (name);
  tree d = ID_SYMBOL (id);
  return d && TREE_CODE (d) == FUNCTION_DECL && fndecl_built_in_p (d);
}

void
record_builtin_type (enum rid rid_index, const char *name, tree type)
{
  tree id = name ? get_identifier (name) : ridpointers[(int) rid_index];
  if (id == NULL_TREE)
    return;
  tree decl = build_decl (UNKNOWN_LOCATION, TYPE_DECL, id, type);
  if (TYPE_NAME (type) == NULL_TREE)
    TYPE_NAME (type) = decl;
  pushdecl (decl);
  if (debug_hooks->type_decl)
    debug_hooks->type_decl (decl, false);
}

tree
c_builtin_function (tree decl)
{
  /* Builtins are visible only under their implementation names
     (__builtin_xxx); the plain-name library functions come from libc2d
     headers.  */
  const char *name = IDENTIFIER_POINTER (DECL_NAME (decl));
  if (name[0] == '_' && name[1] == '_')
    pushdecl (decl);
  return decl;
}

tree
c_builtin_function_ext_scope (tree decl)
{
  return c_builtin_function (decl);
}

tree
c_simulate_builtin_function_decl (tree decl)
{
  return pushdecl (decl);
}

/* GCC core needs a language "convert".  TzD semantic conversions are done
   in tzd-parse.cc; this one handles scalars only, the way the middle end
   uses it.  */
tree
convert (tree type, tree expr)
{
  tree e = expr;
  enum tree_code code = TREE_CODE (type);

  if (type == TREE_TYPE (expr) || TREE_CODE (expr) == ERROR_MARK)
    return expr;
  if (TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (TREE_TYPE (expr)))
    return fold_convert (type, expr);
  switch (code)
    {
    case VOID_TYPE:
      return fold_build1 (CONVERT_EXPR, type, e);
    case BOOLEAN_TYPE:
      return fold_build2 (NE_EXPR, type, e,
			  build_zero_cst (TREE_TYPE (e)));
    case INTEGER_TYPE:
    case ENUMERAL_TYPE:
      return fold (convert_to_integer (type, e));
    case POINTER_TYPE:
    case REFERENCE_TYPE:
      return fold (convert_to_pointer (type, e));
    case REAL_TYPE:
      return fold (convert_to_real (type, e));
    case COMPLEX_TYPE:
      return fold (convert_to_complex (type, e));
    case VECTOR_TYPE:
      return fold (convert_to_vector (type, e));
    case RECORD_TYPE:
    case UNION_TYPE:
      return fold_build1 (VIEW_CONVERT_EXPR, type, e);
    default:
      error ("tzd: conversion not supported");
      return error_mark_node;
    }
}

tree
c_build_qualified_type (tree type, int quals, tree, size_t)
{
  return build_qualified_type (type, quals);
}

int
c_types_compatible_p (tree x, tree y)
{
  return TYPE_MAIN_VARIANT (x) == TYPE_MAIN_VARIANT (y);
}

tree
c_type_promotes_to (tree type)
{
  tree t = TYPE_MAIN_VARIANT (type);
  if (t == float_type_node)
    return double_type_node;
  if (INTEGRAL_TYPE_P (t)
      && TYPE_PRECISION (t) < TYPE_PRECISION (integer_type_node))
    return integer_type_node;
  return type;
}

tree
perform_integral_promotions (tree exp)
{
  tree type = TREE_TYPE (exp);
  if (INTEGRAL_TYPE_P (type)
      && TYPE_PRECISION (type) < TYPE_PRECISION (integer_type_node))
    return fold_convert (integer_type_node, exp);
  return exp;
}

tree
default_conversion (tree exp)
{
  return perform_integral_promotions (exp);
}

tree
decl_constant_value (tree decl)
{
  return decl;
}

bool
lvalue_p (const_tree ref)
{
  switch (TREE_CODE (ref))
    {
    case VAR_DECL: case PARM_DECL: case RESULT_DECL:
    case INDIRECT_REF: case MEM_REF: case ARRAY_REF: case COMPONENT_REF:
    case STRING_CST:
      return true;
    default:
      return false;
    }
}

tree
fold_for_warn (tree x)
{
  return fold (x);
}

tree
c_fully_fold (tree expr, bool, bool *maybe_const, bool)
{
  if (maybe_const)
    *maybe_const = true;
  return fold (expr);
}

bool
c_decl_implicit (const_tree)
{
  return false;
}

bool
has_c_linkage (const_tree)
{
  return true;
}

tree
c_linkage_bindings (tree)
{
  return NULL_TREE;
}

int
maybe_adjust_arg_pos_for_attribute (const_tree)
{
  return 0;
}

bool
instantiation_dependent_expression_p (tree)
{
  return false;
}

alias_set_type
c_get_alias_set (tree t)
{
  if (TREE_CODE (t) == ENUMERAL_TYPE)
    return get_alias_set (ENUM_UNDERLYING_TYPE (t));
  return c_common_get_alias_set (t);
}

int
c_type_dwarf_attribute (const_tree, int)
{
  return -1;
}

bool
c_missing_noreturn_ok_p (tree decl)
{
  return MAIN_NAME_P (DECL_ASSEMBLER_NAME (decl));
}

bool
c_warn_unused_global_decl (const_tree decl)
{
  return TREE_CODE (decl) == FUNCTION_DECL && !TREE_PUBLIC (decl)
	 && !DECL_EXTERNAL (decl);
}

void
c_finish_incomplete_decl (tree)
{
}

void
c_incomplete_type_error (location_t loc, const_tree, const_tree type)
{
  error_at (loc, "invalid use of incomplete type %qT", type);
}

tree
c_reconstruct_complex_type (tree type, tree bottom)
{
  return bottom ? bottom : type;
}

void
c_print_identifier (FILE *, tree, int)
{
}

size_t
c_tree_size (enum tree_code code)
{
  switch (TREE_CODE_CLASS (code))
    {
    case tcc_declaration: return sizeof (tree_decl_non_common);
    case tcc_type: return sizeof (tree_type_non_common);
    default: gcc_unreachable ();
    }
}

void
c_initialize_diagnostics (diagnostic_context *context)
{
  pretty_printer *base = context->printer;
  c_pretty_printer *pp = XNEW (c_pretty_printer);
  context->printer = new (pp) c_pretty_printer ();
  base->~pretty_printer ();
  XDELETE (base);
  c_common_diagnostics_set_defaults (context);
  diagnostic_format_decoder (context) = default_tree_printer;
}

label_text
range_label_for_type_mismatch::get_text (unsigned) const
{
  if (m_labelled_type == NULL_TREE)
    return label_text::borrow (NULL);
  pretty_printer pp;
  pp_printf (&pp, "%s", get_tree_code_name (TREE_CODE (m_labelled_type)));
  return label_text::take (xstrdup (pp_formatted_text (&pp)));
}

/* Statement-tree plumbing used by c-family/c-semantics.cc.  */
static struct stmt_tree_s tzd_stmt_tree;

stmt_tree
current_stmt_tree (void)
{
  return &tzd_stmt_tree;
}

tree
add_stmt (tree t)
{
  return t;
}

/* Preprocess-only (-E) needs nothing from the parser here.  */
void
c_init_preprocess (void)
{
}

/* Pragmas are skipped by the TzD lexer for now.  */
enum cpp_ttype
pragma_lex (tree *value, location_t *)
{
  *value = NULL_TREE;
  return CPP_EOF;
}

void
pragma_lex_discard_to_eol ()
{
}

/* Decimal-float literal state (C only).  */
static bool float_const_decimal64;
bool float_const_decimal64_p (void) { return float_const_decimal64; }
void set_float_const_decimal64 (void) { float_const_decimal64 = true; }
void clear_float_const_decimal64 (void) { float_const_decimal64 = false; }

/* File scope is opened in tzd_init and closed by the parser.  */
void push_file_scope (void) {}
void pop_file_scope (void) {}

void
c_parse_file (void)
{
  tzd_parse_file ();
}

void
c_parse_final_cleanups (void)
{
}

/* ---- entry points C's parser would reach; not used by TzD ---- */

#define TZD_UNSUPPORTED(what) \
  fatal_error (input_location, "tzd: internal: %s is not supported", what)

tree
build_unary_op (location_t, enum tree_code, tree, bool)
{ TZD_UNSUPPORTED ("build_unary_op"); }

tree
build_binary_op (location_t, enum tree_code, tree, tree, bool)
{ TZD_UNSUPPORTED ("build_binary_op"); }

tree
build_indirect_ref (location_t loc, tree ptr, ref_operator)
{
  /* resolve_overloaded_builtin (C11 atomics, task #61) dereferences the raw
     result pointer of a generic __atomic_load/exchange here.  */
  tree type = ptr ? TREE_TYPE (ptr) : NULL_TREE;
  if (type && POINTER_TYPE_P (type))
    return build1_loc (loc, INDIRECT_REF, TREE_TYPE (type), ptr);
  TZD_UNSUPPORTED ("build_indirect_ref");
}

tree
build_modify_expr (location_t, tree, tree, enum tree_code, location_t, tree,
		   tree)
{ TZD_UNSUPPORTED ("build_modify_expr"); }

tree
build_function_call_vec (location_t loc, vec<location_t>, tree function,
			 vec<tree, va_gc> *params, vec<tree, va_gc> *, tree)
{
  /* resolve_overloaded_builtin (C11 atomics / __sync, task #61) builds the
     resolved call through here with a concrete FUNCTION_DECL and typed
     arguments.  Build that call directly; TzD's own expression path handles
     ordinary calls and never reaches this.  */
  if (function && TREE_CODE (function) == FUNCTION_DECL)
    {
      tree rt = TREE_TYPE (TREE_TYPE (function));
      tree addr = build_fold_addr_expr_loc (loc, function);
      unsigned n = vec_safe_length (params);
      tree *argv = XALLOCAVEC (tree, n + 1);
      for (unsigned i = 0; i < n; i++)
	argv[i] = (*params)[i];
      return build_call_array_loc (loc, rt, addr, n, argv);
    }
  TZD_UNSUPPORTED ("build_function_call_vec");
}

tree
common_type (tree, tree)
{ TZD_UNSUPPORTED ("common_type"); }

tree
convert_init (tree, tree)
{ TZD_UNSUPPORTED ("convert_init"); }

tree
lookup_label (tree)
{ TZD_UNSUPPORTED ("lookup_label"); }

tree
c_expr_to_decl (tree, bool *, bool *)
{ TZD_UNSUPPORTED ("c_expr_to_decl"); }

tree
c_omp_clause_copy_ctor (tree, tree, tree)
{ TZD_UNSUPPORTED ("OpenMP"); }

bool
c_var_mod_p (tree, tree)
{
  return false;
}

tree
c_simulate_enum_decl (location_t, const char *, vec<string_int_pair> *)
{ TZD_UNSUPPORTED ("c_simulate_enum_decl"); }

tree
c_simulate_record_decl (location_t, const char *, array_slice<const tree>)
{ TZD_UNSUPPORTED ("c_simulate_record_decl"); }

/* ------------------------------------------------------------------ */
/* Attribute tables (same set C uses).  */

static const attribute_spec std_attributes[] =
{
  { "deprecated", 0, 1, false, false, false, false,
    handle_deprecated_attribute, NULL },
  { "fallthrough", 0, 0, false, false, false, false,
    handle_fallthrough_attribute, NULL },
  { "maybe_unused", 0, 0, false, false, false, false,
    handle_unused_attribute, NULL },
};

extern const scoped_attribute_specs std_attribute_table;
const scoped_attribute_specs std_attribute_table =
{
  nullptr, { std_attributes }
};

/* Named address spaces (i386-c.cc registers __seg_fs/__seg_gs).  */
void
c_register_addr_space (const char *word, addr_space_t as)
{
  int rid = RID_FIRST_ADDR_SPACE + as;
  tree id = get_identifier (word);
  C_SET_RID_CODE (id, rid);
  TREE_LANG_FLAG_0 (id) = 1;   /* C_IS_RESERVED_WORD */
  ridpointers[rid] = id;
}

/* ------------------------------------------------------------------ */
/* Initialisation.  */

static void
init_reswords (void)
{
  int mask = D_CXXONLY | D_OBJC | D_CXX_OBJC;
  if (!flag_isoc99)
    mask |= D_C99;
  if (!flag_isoc23)
    mask |= D_C23;

  ridpointers = ggc_cleared_vec_alloc<tree> ((int) RID_MAX);
  for (unsigned i = 0; i < num_c_common_reswords; i++)
    {
      if (c_common_reswords[i].disable & mask)
	continue;
      tree id = get_identifier (c_common_reswords[i].word);
      C_SET_RID_CODE (id, c_common_reswords[i].rid);
      TREE_LANG_FLAG_0 (id) = 1;   /* C_IS_RESERVED_WORD */
      ridpointers[(int) c_common_reswords[i].rid] = id;
    }
}

extern void tzd_register_passes (void);

bool
tzd_init (void)
{
  location_t save_loc = input_location;

  init_reswords ();
  current_function_decl = NULL_TREE;
  /* The late wipe at each function's exit (tzd-pass.cc).  */
  tzd_register_passes ();

  /* File scope.  */
  tzd_push_scope ();

  input_location = BUILTINS_LOCATION;
  c_common_nodes_and_builtins ();

  truthvalue_type_node = integer_type_node;
  truthvalue_true_node = integer_one_node;
  truthvalue_false_node = integer_zero_node;

  pushdecl (build_decl (UNKNOWN_LOCATION, TYPE_DECL, get_identifier ("_Bool"),
			boolean_type_node));

  /* Fixed-width integer types, built in (task #34).  The width is the one
     in the name regardless of the target (QI=8, HI=16, SI=32, DI=64 bits),
     so int8/int16/int32/int64 (and the unsigned uint* forms) never shift
     with the ABI the way C's int/long/size_t do.  They are ordinary typedef
     names, so they do not reserve the identifier in inner scopes.  */
  {
    static const struct { const char *name; tree *node; } fixed_ints[] = {
      { "int8",   &intQI_type_node },  { "uint8",  &unsigned_intQI_type_node },
      { "int16",  &intHI_type_node },  { "uint16", &unsigned_intHI_type_node },
      { "int32",  &intSI_type_node },  { "uint32", &unsigned_intSI_type_node },
      { "int64",  &intDI_type_node },  { "uint64", &unsigned_intDI_type_node },
    };
    for (unsigned i = 0; i < sizeof fixed_ints / sizeof fixed_ints[0]; i++)
      pushdecl (build_decl (UNKNOWN_LOCATION, TYPE_DECL,
			    get_identifier (fixed_ints[i].name),
			    *fixed_ints[i].node));
  }
  input_location = save_loc;

  return c_common_init ();
}

#include "gt-tzd-tzd-decl.h"
