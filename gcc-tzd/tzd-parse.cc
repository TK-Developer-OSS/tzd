/* TzD front end: parser, type resolution and lowering to GENERIC.

   Managed C rule (task #1): every data pointer is a managed pointer
   object.  Representation (b) strict (provisional until TK adopts it):

   - In memory (variables, struct members, array elements) a `T *` is an
     8-byte slot holding the address, exactly as in C: sizeof, offsets,
     layout and the host ABI are C's.  Its bounds live in libc2d's shadow
     region keyed by the slot address (libc2d/src/shadow.c), out of reach of
     the program.  The slot type is a distinct POINTER_TYPE marked with
     TYPE_LANG_FLAG_1 (TZD_MSLOT_P).
   - As a value (temporaries, parameters, returns, locals whose address is
     never taken) it is the record
         struct { T *addr; void *base; size_t size; }
     (TZD_MREC_P, same layout as libc2d's tzd_ptr).
   - Reading a slot calls tzd_sh_load, which traps unless the slot still holds
     the address recorded by the last tzd_sh_store; writing calls tzd_sh_store
     (address and bounds together).  Struct copies call tzd_sh_copy; static
     initializers queue {slot, base, size} in section tzd_shinit, which
     libc2d registers before main.

   Pointer arithmetic moves addr only; every access through a pointer
   checks that [addr, addr + sizeof (T)) lies inside [base, base + size)
   and calls tzd_trap() otherwise.  Arrays stay native storage; when an array
   decays it becomes a managed pointer covering the whole array.  The
   native address is never visible to user code.

   Function pointers stay native (they are not data).

   Declarations inside system headers keep C's native pointers and
   layouts: they describe the host libc (raw-C boundary, task #24).  Values
   crossing it are converted: managed -> raw passes the address; raw ->
   managed only through a bounds rule (host_ptr_rules) or the bounds user
   code recorded when it stored the pointer -- no default (task #1); T **
   arguments go through libc2d (tzd_pp_in / tzd_pp_out); functions converted to a
   function pointer type of the other ABI get a generated thunk.

   Not yet supported (reported as errors): range designators, VLAs, inline
   asm statements.  GNU attributes: hints are skipped, mode is applied,
   the rest is an error (task #26).  */

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
#include "stor-layout.h"
#include "fold-const.h"
#include "varasm.h"
#include "output.h"
#include "tree-iterator.h"
#include "stmt.h"
#include "attribs.h"
#include "gimplify.h"
#include "builtins.h"
#include "tree-dfa.h"
#include "real.h"
#include "debug.h"
#include "toplev.h"
#include "tzd-tree.h"
#include "c-family/c-pragma.h"
#include "tzd-lex.h"

/* ------------------------------------------------------------------ */
/* Tokens.  The whole translation unit is lexed up front through the
   lexer boundary; the parser only ever sees tzd_token.  */

static vec<tzd_token> toks;
static unsigned tpos;

static const tzd_token &
peek (unsigned k = 0)
{
  unsigned i = tpos + k;
  if (i >= toks.length ())
    i = toks.length () - 1;
  return toks[i];
}

static void
next (void)
{
  if (tpos + 1 < toks.length ())
    tpos++;
}

static location_t
loc0 (void)
{
  return peek ().loc;
}

static bool
is_p (const char *s, unsigned k = 0)
{
  const tzd_token &t = peek (k);
  return t.kind == TZD_PUNCT && strcmp (t.text, s) == 0;
}

/* Keyword or identifier with spelling S (GNU spellings such as __const
   come through as identifiers).  */
static bool
is_w (const char *s, unsigned k = 0)
{
  const tzd_token &t = peek (k);
  return (t.kind == TZD_KEYWORD || t.kind == TZD_IDENT)
	 && strcmp (t.text, s) == 0;
}

static bool
accept (const char *s)
{
  if (is_p (s))
    {
      next ();
      return true;
    }
  return false;
}

static bool
expect (const char *s)
{
  if (accept (s))
    return true;
  error_at (loc0 (), "expected %qs before %qs", s, peek ().text);
  return false;
}

/* Skip to just after the next ';' at nesting level 0 (or before '}').  */
static void
skip_to_semi (void)
{
  int depth = 0;
  while (peek ().kind != TZD_EOF)
    {
      if (is_p ("(") || is_p ("[") || is_p ("{"))
	depth++;
      else if (is_p (")") || is_p ("]") || is_p ("}"))
	{
	  if (depth == 0)
	    return;
	  depth--;
	}
      else if (is_p (";") && depth == 0)
	{
	  next ();
	  return;
	}
      next ();
    }
}

/* Skip a balanced (...) group starting at the current '('.  */
static void
skip_parens (void)
{
  int depth = 0;
  do
    {
      if (is_p ("("))
	depth++;
      else if (is_p (")"))
	depth--;
      next ();
    }
  while (depth > 0 && peek ().kind != TZD_EOF);
}

/* Error recovery: a loop iteration that consumed no token skips one, so a
   parse error can never spin forever.  */
struct progress_guard {
  unsigned start;
  ~progress_guard ()
  {
    if (tpos == start && peek ().kind != TZD_EOF)
      next ();
  }
};
#define PROGRESS_GUARD progress_guard pg_ = { tpos }

/* GNU attributes (task #26).  An attribute is never silently dropped when
   it can change layout or behavior: dropping `packed`, `aligned`, `mode`,
   `vector_size` makes a type disagree with the host's view of the same
   memory, and dropping `cleanup`, `constructor`, `weak`, `alias`, ...
   changes what the program does.  So:
     - hints and diagnostics that only allow more optimization or more
       warnings (attr_ignorable) are skipped;
     - `mode` is implemented (integer modes; glibc's register_t is
       `int __attribute__((mode(word)))`, 64-bit);
     - `aligned` and `packed` are implemented as in GCC's C front end for
       variables, typedefs, struct members and struct/union types (the
       kernel needs them, task #42).  A pointer member that `packed`
       leaves at an offset that is not a multiple of 8 is a compile
       error: the bounds table keys 8-byte aligned slots;
     - anything else is an error until it is implemented.  */
static const char *const attr_ignorable[] = {
  "nothrow", "leaf", "nonnull", "access", "pure", "malloc", "const",
  "format", "format_arg", "deprecated", "unavailable",
  "alloc_size", "alloc_align", "warn_unused_result", "cold", "hot",
  "returns_nonnull", "sentinel", "nonstring",
  "gnu_inline", "artificial", "flatten",
  "noclone", "no_instrument_function", "warning", "error",
  "fd_arg", "fd_arg_read", "fd_arg_write", "null_terminated_string_arg",
  "no_reorder", "maybe_unused",
  /* Dropping these only loses an optimization, a diagnostic or a
     sanitizer/profiler setting TzD does not use (Linux kernel).  */
  "designated_init", "nonstring", "assume_aligned", "uninitialized",
  "fallthrough", "counted_by", "no_sanitize", "no_sanitize_address",
  "no_sanitize_thread", "no_sanitize_undefined", "no_sanitize_coverage",
  "disable_sanitizer_instrumentation", "no_profile_instrument_function",
  "no_split_stack", "optimize", "expected_throw", 0
};

/* Attributes given to GCC's own handlers (decl_attributes), so they mean
   exactly what they mean in C: they change where code and data go, how a
   symbol is linked or how a function is entered, not what memory a
   pointer may reach (task #42, Linux kernel).  */
static const char *const attr_passthrough[] = {
  "section", "visibility", "weak", "alias", "weakref", "externally_visible",
  "naked", "noipa", "no_stack_protector", "stack_protect",
  "patchable_function_entry", "nocf_check", "indirect_branch",
  "function_return", "no_caller_saved_registers", "retain", "noplt",
  "no_icf", "target", "symver", "ms_abi", "sysv_abi", "regparm",
  /* A call to it does not return (code after it is unreachable): C's
     meaning; the kernel's BUG()/panic()/do_exit() paths need it for
     -Wreturn-type.  */
  "noreturn",
  /* Inlining the kernel depends on: asm "i" operands in __always_inline
     functions (static_cpu_has, rip_rel_ptr) are constants only once the
     call is inlined; noinline keeps a function out of line.  */
  "always_inline", "noinline",
  /* `used` keeps a static object that nothing references (the kernel's
     initcall and __ADDRESSABLE tables); `unused` silences the warning for
     a static function nothing calls (the kernel's `inline`).  */
  "used", "unused", 0
};

/* Pending pass-through attributes (a TREE_LIST as decl_attributes takes
   it), taken by the declaration they belong to.  */
static GTY(()) tree attr_decl_list;
static location_t attr_decl_loc;

/* Pending cleanup (fn) attribute: the function, taken by the variable
   declaration it belongs to.  */
static GTY(()) tree attr_cleanup_fn;
static location_t attr_cleanup_loc;
/* make_var: the variable is used by address (cleanup), keep it in memory.  */
static bool force_slot;
static void push_cleanup_call (tree fn, tree var, location_t loc);

/* A pending __mode__ attribute: the next type built by finish_declspecs or
   apply_declarator takes it (apply_attr_mode).  */
static machine_mode attr_mode = VOIDmode;
static location_t attr_mode_loc;

/* Pending aligned (bytes, 0 = none) and packed attributes: taken by the
   declaration, member or struct they belong to (take_attr_layout).  */
struct attr_layout { unsigned align; bool packed; location_t loc;
		     bool transparent; bool may_alias; };
static attr_layout attr_lay = { 0, false, UNKNOWN_LOCATION, false, false };

static attr_layout
take_attr_layout (void)
{
  attr_layout a = attr_lay;
  attr_lay.align = 0;
  attr_lay.packed = false;
  attr_lay.transparent = false;
  attr_lay.may_alias = false;
  return a;
}

static machine_mode
attr_mode_by_name (const char *n)
{
  if (strcmp (n, "word") == 0)
    return word_mode;
  if (strcmp (n, "pointer") == 0)
    return ptr_mode;
  if (strcmp (n, "byte") == 0)
    return byte_mode;
  for (int i = 0; i < NUM_MACHINE_MODES; i++)
    if (strcmp (GET_MODE_NAME (i), n) == 0)
      return (machine_mode) i;
  return VOIDmode;
}

/* Attribute name without the optional __...__ around it.  */
static const char *
attr_bare_name (const char *n, char *buf, size_t len)
{
  size_t l = strlen (n);
  if (l > 4 && n[0] == '_' && n[1] == '_' && n[l - 1] == '_'
      && n[l - 2] == '_' && l - 4 < len)
    {
      memcpy (buf, n + 2, l - 4);
      buf[l - 4] = 0;
      return buf;
    }
  return n;
}

static tree parse_const_int (void);
static tree attr_string_arg (void);

/* __attribute__ (( a, b (args), ... )), the keyword already consumed.
   MODE_OK: a type follows that can take a mode attribute.  */
/* A `gnu_inline` attribute was read since the last function definition:
   its `inline` follows the gnu89 rules whatever -std says (gnulib's
   gettext.h fallbacks: `extern inline` with the attribute is inline
   only).  */
static bool attr_gnu_inline;

static void
parse_attribute_list (bool mode_ok)
{
  location_t loc = loc0 ();
  if (!accept ("(") || !accept ("("))
    {
      error_at (loc, "expected %<((%> after %<__attribute__%>");
      return;
    }
  while (!is_p (")") && peek ().kind != TZD_EOF)
    {
      PROGRESS_GUARD;
      if (accept (","))
	continue;
      const tzd_token &t = peek ();
      if (t.kind != TZD_IDENT && t.kind != TZD_KEYWORD)
	{
	  error_at (t.loc, "expected attribute name");
	  skip_parens ();
	  continue;
	}
      char buf[64];
      const char *name = attr_bare_name (t.text, buf, sizeof buf);
      location_t al = t.loc;
      next ();
      if (strcmp (name, "gnu_inline") == 0)
	attr_gnu_inline = true;   /* (otherwise ignorable) */
      if (strcmp (name, "mode") == 0)
	{
	  machine_mode m = VOIDmode;
	  if (accept ("(") && peek ().kind == TZD_IDENT)
	    {
	      char mb[64];
	      m = attr_mode_by_name (attr_bare_name (peek ().text, mb,
						     sizeof mb));
	      next ();
	      expect (")");
	    }
	  if (m == VOIDmode)
	    error_at (al, "unknown machine mode in attribute %<mode%>");
	  else if (!mode_ok)
	    error_at (al, "attribute %<mode%> is not supported here (task #26)");
	  else
	    {
	      attr_mode = m;
	      attr_mode_loc = al;
	    }
	  continue;
	}
      if (strcmp (name, "aligned") == 0)
	{
	  unsigned n = BIGGEST_ALIGNMENT / BITS_PER_UNIT;
	  if (accept ("("))
	    {
	      tree v = parse_const_int ();
	      expect (")");
	      n = 0;
	      if (v && tree_fits_uhwi_p (v))
		{
		  unsigned HOST_WIDE_INT u = tree_to_uhwi (v);
		  if (u && (u & (u - 1)) == 0 && u <= (1u << 28))
		    n = (unsigned) u;
		}
	      if (!n)
		error_at (al, "requested alignment is not a positive power of 2");
	    }
	  if (n > attr_lay.align)
	    attr_lay.align = n;
	  attr_lay.loc = al;
	  continue;
	}
      if (strcmp (name, "packed") == 0)
	{
	  attr_lay.packed = true;
	  attr_lay.loc = al;
	  continue;
	}
      if (strcmp (name, "may_alias") == 0)
	{
	  /* An lvalue of the type may alias any object (GCC's type-based
	     alias analysis gives it alias set 0): a typedef takes it.  It
	     changes no layout and no bounds.  */
	  attr_lay.may_alias = true;
	  attr_lay.loc = al;
	  continue;
	}
      if (strcmp (name, "transparent_union") == 0)
	{
	  /* A union parameter that accepts any of its member types (GCC).  */
	  attr_lay.transparent = true;
	  attr_lay.loc = al;
	  continue;
	}
      if (strcmp (name, "cleanup") == 0)
	{
	  /* cleanup (fn): fn (&var) when var leaves its scope.  */
	  tree fn = NULL_TREE;
	  if (accept ("(") && peek ().kind == TZD_IDENT)
	    {
	      fn = tzd_lookup (get_identifier (peek ().text));
	      next ();
	      expect (")");
	    }
	  if (!fn || TREE_CODE (fn) != FUNCTION_DECL)
	    error_at (al, "attribute %<cleanup%> needs a function name");
	  else
	    {
	      attr_cleanup_fn = fn;
	      attr_cleanup_loc = al;
	    }
	  continue;
	}
      bool pass = false;
      for (const char *const *w = attr_passthrough; *w; w++)
	if (strcmp (*w, name) == 0)
	  pass = true;
      if (pass)
	{
	  tree args = NULL_TREE;
	  if (accept ("("))
	    {
	      while (!is_p (")") && peek ().kind != TZD_EOF)
		{
		  PROGRESS_GUARD;
		  tree a;
		  if (peek ().kind == TZD_STRING)
		    a = attr_string_arg ();
		  else
		    a = parse_const_int ();
		  if (a)
		    args = chainon (args, build_tree_list (NULL_TREE, a));
		  if (!accept (","))
		    break;
		}
	      expect (")");
	    }
	  attr_decl_list = chainon (attr_decl_list,
				    build_tree_list (get_identifier (name),
						     args));
	  attr_decl_loc = al;
	  continue;
	}
      bool known = false;
      for (const char *const *w = attr_ignorable; *w; w++)
	if (strcmp (*w, name) == 0)
	  known = true;
      /* Under `#pragma tzd returns_twice` the attribute is accepted on a
	 declaration (it reaches TzD only via pthread.h in OpenSSL, which
	 never calls the setjmp-family function).  This does NOT make an
	 actual setjmp/longjmp safe: a longjmp skips scope exits and so the
	 two-phase wipe -- correct handling of a real call is a separate task
	 (task #26).  */
      extern bool tzd_pragma_returns_twice;
      if (!known && tzd_pragma_returns_twice
	  && strcmp (name, "returns_twice") == 0)
	known = true;
      if (!known)
	error_at (al, "attribute %qs is not supported: it can change layout "
		  "or behavior, so it is not ignored (task #26)", name);
      if (is_p ("("))
	skip_parens ();
    }
  expect (")");
  expect (")");
}

/* __attribute__((...)) and __extension__.  */
static bool
skip_gnu_noise (bool mode_ok = true)
{
  bool any = false;
  for (;;)
    {
      if (is_w ("__attribute__") || is_w ("__attribute"))
	{
	  next ();
	  parse_attribute_list (mode_ok);
	  any = true;
	}
      else if (is_w ("__extension__"))
	{
	  next ();
	  any = true;
	}
      else
	return any;
    }
}

/* Give TYPE the pending mode attribute, if any.  */
static tree
apply_attr_mode (tree type)
{
  if (attr_mode == VOIDmode)
    return type;
  machine_mode m = attr_mode;
  attr_mode = VOIDmode;
  if (type == error_mark_node)
    return type;
  if (!INTEGRAL_TYPE_P (type) || TREE_CODE (type) == BOOLEAN_TYPE
      || GET_MODE_CLASS (m) != MODE_INT)
    {
      error_at (attr_mode_loc, "attribute %<mode%> is supported only for "
		"integer types and integer modes (task #26)");
      return error_mark_node;
    }
  tree nt = c_common_type_for_mode (m, TYPE_UNSIGNED (type));
  if (!nt)
    {
      error_at (attr_mode_loc, "no integer type for this mode");
      return error_mark_node;
    }
  return build_qualified_type (nt, TYPE_QUALS (type));
}

/* A mode attribute that no type took is an error, not a silent drop.  */
static void
check_attr_mode_used (void)
{
  if (attr_mode != VOIDmode)
    {
      error_at (attr_mode_loc, "attribute %<mode%> does not apply to a type "
		"here (task #26)");
      attr_mode = VOIDmode;
    }
  attr_layout a = take_attr_layout ();
  if (a.align || a.packed || a.transparent || a.may_alias)
    error_at (a.loc, "attribute %<%s%> does not apply here (task #26)",
	      a.may_alias ? "may_alias"
	      : a.transparent ? "transparent_union"
	      : a.packed ? "packed" : "aligned");
  if (attr_decl_list)
    {
      error_at (attr_decl_loc, "attribute %qE does not apply here (task #26)",
		TREE_PURPOSE (attr_decl_list));
      attr_decl_list = NULL_TREE;
    }
  if (attr_cleanup_fn)
    {
      error_at (attr_cleanup_loc, "attribute %<cleanup%> does not apply here");
      attr_cleanup_fn = NULL_TREE;
    }
}

/* Give DECL the pending pass-through attributes (GCC's handlers).  */
static void
apply_attr_decl (tree decl, tree list)
{
  if (!list || decl == error_mark_node)
    return;
  decl_attributes (&decl, list, 0);
  /* An alias is emitted by rest_of_decl_compilation (as in C).  */
  if (TREE_CODE (decl) == FUNCTION_DECL
      && (lookup_attribute ("alias", DECL_ATTRIBUTES (decl))
	  || lookup_attribute ("weakref", DECL_ATTRIBUTES (decl))))
    rest_of_decl_compilation (decl, 1, 0);
}

/* ------------------------------------------------------------------ */
/* Managed pointer types.  */

static GTY(()) vec<tree, va_gc> *mptr_cache;   /* pointee, record pairs */
static GTY(()) vec<tree, va_gc> *mslot_cache;  /* pointee, slot type pairs */

static inline bool
error_p_tree (tree t)
{
  return t == error_mark_node;
}
static GTY(()) tree tzd_trap_decl;
/* ABI thunks: (function decl, native function type, thunk decl) triples.  */
static GTY(()) vec<tree, va_gc> *thunk_cache;
static GTY(()) tree tzd_pp_in_decl;
/* Host functions redirected to libc2d's checked versions: (host, libc2d) decl
   pairs.  */
static GTY(()) vec<tree, va_gc> *libd_redirect_cache;
static GTY(()) tree tzd_pp_out_decl;
/* libc2d shadow-region entry points (task #1 (b)).  */
static GTY(()) tree tzd_sh_load_decl;
static GTY(()) tree tzd_sh_load_lazy_decl;
static GTY(()) tree tzd_sh_record_typed_decl;
static GTY(()) tree tzd_sh_load_typed_decl;
/* {slot, base, size, id} for static initializers of fnptr_holds members
   (section tzd_shtyped, task #70).  */
static GTY(()) vec<tree, va_gc> *shtyped_list;
static GTY(()) tree tzd_sh_store_decl;
static GTY(()) tree tzd_sh_copy_decl;
static GTY(()) tree tzd_sh_wipe_decl;
static GTY(()) tree tzd_sh_wipe_data_decl;
static GTY(()) tree tzd_sh_record_decl;
static GTY(()) tree tzd_sh_match_decl;
/* Function-pointer interface registry (task #61, resolving #22).  */
static GTY(()) tree tzd_fn_reg_decl;
static GTY(()) tree tzd_fn_check_decl;
/* Static pointer initializers to register at startup: (slot address,
   base, size) triples, emitted into section tzd_shinit at the end of the
   translation unit.  */
static GTY(()) vec<tree, va_gc> *shinit_list;
static void emit_shinit (void);
/* (function address, interface id) pairs for functions erased to the generic
   void(*)(void) in static initializers (dispatch tables): registered at
   startup from section tzd_fninit, since a tzd_fn_reg call is not a constant
   (task #61).  */
static GTY(()) vec<tree, va_gc> *fninit_list;
static void emit_fninit (void);
static void emit_shtyped (void);

static void
mark_addressable_tree (tree t)
{
  while (handled_component_p (t))
    t = TREE_OPERAND (t, 0);
  if (TREE_CODE (t) == TARGET_EXPR)
    t = TARGET_EXPR_SLOT (t);    /* compound literal */
  if (DECL_P (t))
    TREE_ADDRESSABLE (t) = 1;
}

/* Value record {T *addr; void *base; size_t size} of a managed pointer to
   POINTEE (same layout as libc2d's tzd_ptr).  */
tree
tzd_mrec_type (tree pointee)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (mptr_cache); i += 2)
    if ((*mptr_cache)[i] == pointee)
      return (*mptr_cache)[i + 1];

  tree rec = make_node (RECORD_TYPE);
  tree f_addr = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier ("addr"),
			    build_pointer_type (pointee));
  tree f_base = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier ("base"), ptr_type_node);
  tree f_size = build_decl (BUILTINS_LOCATION, FIELD_DECL,
			    get_identifier ("size"), size_type_node);
  DECL_CONTEXT (f_addr) = DECL_CONTEXT (f_base) = DECL_CONTEXT (f_size) = rec;
  DECL_CHAIN (f_addr) = f_base;
  DECL_CHAIN (f_base) = f_size;
  TYPE_FIELDS (rec) = f_addr;
  TYPE_NAME (rec) = get_identifier ("__tzd_ptr");
  TYPE_LANG_FLAG_0 (rec) = 1;
  TYPE_LANG_SLOT_1 (rec) = pointee;
  layout_type (rec);

  vec_safe_push (mptr_cache, pointee);
  vec_safe_push (mptr_cache, rec);
  return rec;
}

/* Slot (language) type of a managed pointer to POINTEE: a distinct 8-byte
   pointer type whose bounds live in libc2d's shadow region (task #1 (b)).  */
tree
tzd_mptr_type (tree pointee)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (mslot_cache); i += 2)
    if ((*mslot_cache)[i] == pointee)
      return (*mslot_cache)[i + 1];
  tree s = build_distinct_type_copy (build_pointer_type (pointee));
  TYPE_LANG_FLAG_1 (s) = 1;
  TZD_MSLOT_REC (s) = tzd_mrec_type (pointee);
  TYPE_CANONICAL (s) = TYPE_CANONICAL (build_pointer_type (pointee));
  vec_safe_push (mslot_cache, pointee);
  vec_safe_push (mslot_cache, s);
  return s;
}

/* Value record type of managed pointer type TYPE (either form).  */
static tree
mrec (tree type)
{
  tree m = TYPE_MAIN_VARIANT (type);
  return TZD_MSLOT_P (m) ? TZD_MSLOT_REC (m) : m;
}

/* Slot (language) form of managed pointer type TYPE, keeping qualifiers.  */
static tree
mslot (tree type)
{
  if (TZD_MSLOT_P (type))
    return type;
  tree s = tzd_mptr_type (TZD_MPTR_TARGET (type));
  int q = TYPE_QUALS (type);
  return q ? build_qualified_type (s, q) : s;
}

/* GENERIC type of a value of language type TYPE.  */
static tree
vtype (tree type)
{
  if (type && !error_p_tree (type) && TZD_MPTR_P (type))
    return mrec (type);
  return type;
}

static tree
mp_field (tree type, int n)
{
  tree f = TYPE_FIELDS (mrec (type));
  while (n-- > 0)
    f = DECL_CHAIN (f);
  return f;
}

static tree
mp_native (tree type)
{
  return TREE_TYPE (mp_field (type, 0));
}

/* Is the declaration being parsed in a system header?  Those keep C's
   native pointers and layouts: they describe the host libc ABI (the raw-C
   boundary; provisional, task #14).  Macros from system headers expanded
   in user code count as user code.
   Inside a static declaration in a system header (sys_static_guard): a
   static function there is compiled here as part of this unit (no host
   code behind it, internal linkage), so it is TzD code with managed
   pointers, checked like the rest (the kernel uapi's swab.h inlines read
   *p; as host code that would be an unbounded pointer, task #24).  */
static int sys_static_depth;


static bool
native_region_p (void)
{
  if (sys_static_depth)
    return false;
  location_t loc = toks[tpos > 0 ? tpos - 1 : 0].loc;
  loc = linemap_resolve_location (line_table, loc,
				  LRK_MACRO_EXPANSION_POINT, NULL);
  return in_system_header_at (loc);
}

/* Pointer to TYPE as a TzD type: managed for data, native for code and
   inside system headers.  */
static tree
make_pointer (tree type)
{
  if (TREE_CODE (type) == FUNCTION_TYPE || native_region_p ())
    return build_pointer_type (type);
  return tzd_mptr_type (type);
}

/* Do two function types differ in pointer ABI (managed vs. raw) in the
   return value or a parameter?  */
static bool
fn_abi_mismatch_p (tree a, tree b)
{
  tree ra = TREE_TYPE (a), rb = TREE_TYPE (b);
  if (TZD_MPTR_P (ra) != TZD_MPTR_P (rb))
    return true;
  tree pa = TYPE_ARG_TYPES (a), pb = TYPE_ARG_TYPES (b);
  for (; pa && pb; pa = TREE_CHAIN (pa), pb = TREE_CHAIN (pb))
    if (TZD_MPTR_P (TREE_VALUE (pa)) != TZD_MPTR_P (TREE_VALUE (pb)))
      return true;
  return false;
}

/* Raw data pointer (from the host libc side), not code, not va_list.
   Pointers to non-default address spaces (__user, __iomem, __percpu, …)
   are managed by the kernel through its own mechanisms and must not be
   promoted to TzD managed slots (task #75).  */
static bool
raw_data_ptr_p (tree type)
{
  return (TREE_CODE (type) == POINTER_TYPE && !TZD_MSLOT_P (type)
	  && TYPE_ADDR_SPACE (TREE_TYPE (type)) == ADDR_SPACE_GENERIC
	  && TREE_CODE (TREE_TYPE (type)) != FUNCTION_TYPE
	  && !(va_list_type_node
	       && TREE_CODE (va_list_type_node) == ARRAY_TYPE
	       && TYPE_MAIN_VARIANT (TREE_TYPE (type))
		  == TYPE_MAIN_VARIANT (TREE_TYPE (va_list_type_node))));
}

static bool
fnptr_p (tree type)
{
  return TREE_CODE (type) == POINTER_TYPE
	 && TREE_CODE (TREE_TYPE (type)) == FUNCTION_TYPE;
}

static bool
error_p (tree t)
{
  return t == NULL_TREE || t == error_mark_node;
}

/* ------------------------------------------------------------------ */
/* Expressions.

   A managed pointer value is either "whole" (t is a record-typed tree) or
   in "parts" (t == NULL; pa/pb/ps are addr/base/size trees).  An lvalue
   produced by dereferencing a managed pointer remembers its unchecked
   origin (oa/ob/os) so that &*p and &p[i] do not trap on one-past-end.  */

struct ex {
  tree t;
  tree type;
  bool lv;
  tree pa, pb, ps;
  tree oa, ob, os;
  /* Member lvalue (s.f / p->f): &E covers only the member (task #21).  */
  bool sub;
  /* With sub: the bounds of &E when wider than E (a queue.h link pair,
     queue_link_first_p); NULL_TREE = E's own size.  */
  tree subsize;
  /* Trailing flexible-array idiom member (data[] / data[0] / data[1]):
     decays to a pointer reaching the end of the enclosing allocation
     (ob/os when known).  */
  bool tail;
  /* Lvalue based on a constant null pointer ((T *)0)->f: only its address
     is usable (offsetof idiom); reading it traps.  */
  bool nullbase;
  /* An integer constant expression in the C sense (literals, enumeration
     constants, sizeof, and operators/casts over those) -- not merely a
     value that folds to a constant: (long) x * 0 folds to 0 but is not
     one.  Decides null pointer constants (the kernel's __is_constexpr).  */
  bool ice;
  /* A managed data pointer cast to a function pointer type: its value
     record and pointee type, for a store into a `#pragma tzd fnptr_holds`
     member (task #70).  */
  tree fdata, fdtype;
};

/* Inside sizeof / typeof / __alignof__ / _Generic's controlling
   expression: the operand is not evaluated.  */
static int unevaluated;
/* Inside the initializer of an object with static storage.  */
static int static_init_depth;

static ex
mk (tree t, tree type, bool lv = false)
{
  ex e;
  memset (&e, 0, sizeof e);
  e.t = t;
  /* Pointer values carry the language (slot) type; their trees have the
     record type.  */
  if (type && type != error_mark_node && TZD_MREC_P (type))
    type = mslot (type);
  e.type = type;
  e.lv = lv;
  return e;
}

static ex
err (void)
{
  return mk (error_mark_node, error_mark_node);
}

static bool
bad (const ex &e)
{
  return error_p (e.type) || (e.t == error_mark_node);
}

static ex
mk_parts (tree type, tree a, tree b, tree s)
{
  ex e = mk (NULL_TREE, type);
  e.pa = fold_convert (mp_native (type), a);
  e.pb = fold_convert (ptr_type_node, b);
  e.ps = fold_convert (size_type_node, s);
  return e;
}

static bool
null_const_p (const ex &e)
{
  return e.t && TREE_CODE (e.t) == INTEGER_CST && integer_zerop (e.t)
	 && INTEGRAL_TYPE_P (e.type);
}

static tree trap_call (location_t loc, const char *what);
static tree mem_lvalue_addr (tree l, location_t loc);
static tree hp_access (tree addr, tree n, bool load, location_t loc);
static tree hp_store_mark (tree l, tree type, location_t loc);

static tree
libd_decl (const char *name, tree type)
{
  tree d = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
		       get_identifier (name), type);
  DECL_EXTERNAL (d) = 1;
  TREE_PUBLIC (d) = 1;
  DECL_ARTIFICIAL (d) = 1;
  TREE_NOTHROW (d) = 1;
  return d;
}

/* Give runtime function D the GCC "fn spec" SPEC (attr-fnspec.h): which
   pointer arguments it only reads or writes without keeping them.  */
static void
set_fnspec (tree d, const char *spec)
{
  tree a = tree_cons (get_identifier ("fn spec"),
		      build_tree_list (NULL_TREE,
				       build_string (strlen (spec), spec)),
		      TYPE_ATTRIBUTES (TREE_TYPE (d)));
  TREE_TYPE (d) = build_type_attribute_variant (TREE_TYPE (d), a);
}

static void
init_shadow_decls (void)
{
  if (tzd_sh_load_decl)
    return;
  tree vr = tzd_mrec_type (void_type_node);
  tzd_sh_load_decl = libd_decl ("tzd_sh_load",
			      build_function_type_list (vr, const_ptr_type_node,
							NULL_TREE));
  /* Reads memory only (or traps): lets GCC reuse a loaded pointer.  */
  DECL_PURE_P (tzd_sh_load_decl) = 1;
  tzd_sh_load_lazy_decl = libd_decl ("tzd_sh_load_lazy",
				     build_function_type_list
				       (vr, const_ptr_type_node, NULL_TREE));
  DECL_PURE_P (tzd_sh_load_lazy_decl) = 1;
  tzd_sh_record_typed_decl = libd_decl ("tzd_sh_record_typed",
					build_function_type_list
					  (void_type_node, ptr_type_node, vr,
					   size_type_node, NULL_TREE));
  tzd_sh_load_typed_decl = libd_decl ("tzd_sh_load_typed",
				      build_function_type_list
					(vr, const_ptr_type_node,
					 size_type_node, NULL_TREE));
  tzd_sh_store_decl = libd_decl ("tzd_sh_store",
			       build_function_type_list (void_type_node,
							 ptr_type_node, vr,
							 NULL_TREE));
  tzd_sh_wipe_decl = libd_decl ("tzd_sh_wipe",
			      build_function_type_list (void_type_node,
							ptr_type_node,
							size_type_node,
							NULL_TREE));
  tzd_sh_copy_decl = libd_decl ("tzd_sh_copy",
			      build_function_type_list (void_type_node,
							ptr_type_node,
							const_ptr_type_node,
							size_type_node,
							NULL_TREE));
  /* Neither keeps the address it is given (libc2d, kc2d): the wipe
     writes the bytes at P ('w': may also read, so no store before it
     counts as dead), the copy moves bounds entries only and does not
     touch DST or SRC ('R').  Without this every wiped local escapes:
     GCC then assumes any call may change it and stops propagating its
     value (the kernel relies on that to drop calls under conditions
     that are constant false).  Not "pure" or "const": both change the
     bounds table.  */
  set_fnspec (tzd_sh_wipe_decl, ". w . ");
  /* The same destruction of the bytes without the removal of bounds
     entries: for memory that cannot hold any (the late pass).  */
  tzd_sh_wipe_data_decl = libd_decl ("tzd_sh_wipe_data",
				   build_function_type_list (void_type_node,
							     ptr_type_node,
							     size_type_node,
							     NULL_TREE));
  set_fnspec (tzd_sh_wipe_data_decl, ". w . ");
  set_fnspec (tzd_sh_copy_decl, ". R R . ");
  /* Raw pointer fields of host structs written by user code: the bounds
     are recorded at the field (the 8 bytes are written by the store).  */
  tzd_sh_record_decl = libd_decl ("tzd_sh_record",
				build_function_type_list (void_type_node,
							  const_ptr_type_node,
							  vr, NULL_TREE));
  tzd_sh_match_decl = libd_decl ("tzd_sh_match",
			       build_function_type_list (integer_type_node,
							 const_ptr_type_node,
							 NULL_TREE));
  DECL_PURE_P (tzd_sh_match_decl) = 1;
  /* The store writes the 8-byte slot and records its bounds; the record
     does not touch the slot (it holds the value already).  A struct
     holding a pointer slot does not escape through them either.  */
  set_fnspec (tzd_sh_store_decl, ". w . ");
  set_fnspec (tzd_sh_record_decl, ". R . ");
  /* Function-pointer interface registry (task #61).  The arguments are a
     code address and a scalar id; neither is read or written as data, so
     ". . . " lets GCC keep optimizing around the call (the registry is
     internal global state, changed by the call but not reachable through
     its arguments).  */
  tzd_fn_reg_decl = libd_decl ("tzd_fn_reg",
			       build_function_type_list (void_type_node,
							 const_ptr_type_node,
							 long_unsigned_type_node,
							 NULL_TREE));
  tzd_fn_check_decl = libd_decl ("tzd_fn_check",
				 build_function_type_list (void_type_node,
							   const_ptr_type_node,
							   long_unsigned_type_node,
							   NULL_TREE));
  set_fnspec (tzd_fn_reg_decl, ". . . ");
  set_fnspec (tzd_fn_check_decl, ". . . ");
}

/* Managed pointer of TYPE for raw address ADDR that comes from outside
   Managed C (an integer, an asm output, a native function).  In kernel
   mode (-ftzd-kernel) the runtime's tzd_from_native gives the bounds of
   the memory region ADDR lies in (and traps when it lies in none);
   otherwise there is no allocation behind it and any access traps.  */
static GTY(()) tree tzd_from_native_decl;
static bool const_addr_split (tree t, tree *base, poly_int64 *off);
static void parts (ex &e);
static ex
from_native (tree addr, tree type, location_t loc)
{
  addr = fold_convert (mp_native (type), addr);
  /* Kernel: an integer that is a constant address into a declared object
     ((void *)(unsigned long)&gdt_page.gdt) gets that object's bounds; it
     stays a constant (an "i" asm operand, a static initializer).  */
  tree obj;
  poly_int64 off;
  if (flag_tzd_kernel && const_addr_split (addr, &obj, &off)
      && VAR_P (obj) && (TREE_STATIC (obj) || DECL_EXTERNAL (obj))
      && COMPLETE_TYPE_P (TREE_TYPE (obj))
      && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (obj))) == INTEGER_CST)
    {
      mark_addressable_tree (obj);
      return mk_parts (type, addr,
		       fold_convert (ptr_type_node,
				     build_fold_addr_expr_loc (loc, obj)),
		       fold_convert (size_type_node,
				     TYPE_SIZE_UNIT (TREE_TYPE (obj))));
    }
  /* A static initializer (or anything outside a function) cannot call
     the runtime: no bounds there.  */
  if (!flag_tzd_kernel || static_init_depth > 0 || !current_function_decl)
    return mk_parts (type, addr, null_pointer_node, size_zero_node);
  bool cst = TREE_CODE (fold (addr)) == INTEGER_CST;
  if (cst && integer_zerop (fold (addr)))
    return mk_parts (type, addr, null_pointer_node, size_zero_node);
  if (!tzd_from_native_decl)
    {
      tree vr = tzd_mrec_type (void_type_node);
      tzd_from_native_decl
	= libd_decl ("tzd_from_native",
		     build_function_type_list (vr, const_ptr_type_node,
					       NULL_TREE));
      DECL_PURE_P (tzd_from_native_decl) = 1;
    }
  tree c = build_call_expr_loc (loc, tzd_from_native_decl, 1,
				fold_convert (const_ptr_type_node, addr));
  ex r = mk (build1 (VIEW_CONVERT_EXPR, mrec (type), c), type);
  if (cst)
    {
      /* A constant address (__FORCE_ORDER's *(int *)0x1000, a sentinel
	 such as (void *) 1) is bounded like any other integer, by the
	 region it lies in (a sentinel lies in none: no bounds); the
	 address itself stays a constant (case (unsigned long) SEND_SIG_PRIV,
	 an "i" operand).  */
      parts (r);
      r.pa = addr;
    }
  return r;
}

/* Functions that use the native C calling convention (-ftzd-native=FILE):
   written in assembly, or called from assembly.  Their pointer parameters
   and result are raw pointers; callers pass pointer() and a raw result
   becomes managed through from_native.  */
static hash_set<tree> *native_names;
/* Functions this unit's asm calls (asm_prescan): native too, but only
   here; a public one must also be in the list (native_from_asm).  */
static hash_set<tree> *asm_native_names;

static bool native_list_p (tree id);

static bool
native_name_p (tree id)
{
  if (!id)
    return false;
  if (asm_native_names && asm_native_names->contains (id))
    return true;
  return native_list_p (id);
}

static bool
native_list_p (tree id)
{
  if (!id)
    return false;
  if (!native_names)
    {
      native_names = new hash_set<tree>;
      if (!tzd_native_file)
	return false;
      FILE *f = fopen (tzd_native_file, "r");
      if (!f)
	{
	  error ("cannot read the native function list %qs",
		 tzd_native_file);
	  return false;
	}
      char line[256];
      while (fgets (line, sizeof line, f))
	{
	  size_t n = strcspn (line, " \t\r\n#");
	  if (n)
	    native_names->add (get_identifier_with_length (line, n));
	}
      fclose (f);
    }
  return native_names->contains (id);
}

/* TYPE (a function type) with its top-level managed pointer parameters and
   result made raw.  */
static tree
native_fn_type (tree type)
{
  if (TREE_CODE (type) != FUNCTION_TYPE)
    return type;
  auto raw = [] (tree t) {
    return TZD_MPTR_P (t)
	   ? build_qualified_type (build_pointer_type (TZD_MPTR_TARGET (t)),
				   TYPE_QUALS (t))
	   : t;
  };
  if (!prototype_p (type))
    return build_function_type (raw (TREE_TYPE (type)), NULL_TREE);
  vec<tree> args = vNULL;
  bool varargs = stdarg_p (type);
  for (tree a = TYPE_ARG_TYPES (type);
       a && TREE_VALUE (a) != void_type_node; a = TREE_CHAIN (a))
    args.safe_push (raw (TREE_VALUE (a)));
  tree nt = varargs
	    ? build_varargs_function_type_array (raw (TREE_TYPE (type)),
						 args.length (),
						 args.address ())
	    : build_function_type_array (raw (TREE_TYPE (type)),
					 args.length (), args.address ());
  args.release ();
  return build_type_attribute_qual_variant (nt, TYPE_ATTRIBUTES (type),
					    TYPE_QUALS (type));
}

/* TYPE (a function type) with its top-level raw data-pointer parameters and
   result made managed: the inverse of native_fn_type.  Used for a libc name
   the program itself replaces with a Managed C definition (the openbsd-compat
   substitutes, managed_override_name_p): a system header (resolv.h) declares
   it with raw pointers, but the linked body is Managed C, so callers must use
   the managed ABI too, or they pass raw pointers to a record-expecting body
   (b64_ntop, task #59).  */
static tree
managed_fn_type (tree type)
{
  if (TREE_CODE (type) != FUNCTION_TYPE)
    return type;
  auto mgd = [] (tree t) {
    return raw_data_ptr_p (t)
	   ? build_qualified_type (tzd_mptr_type (TREE_TYPE (t)), TYPE_QUALS (t))
	   : t;
  };
  if (!prototype_p (type))
    return type;                 /* no parameter types to convert */
  vec<tree> args = vNULL;
  bool varargs = stdarg_p (type);
  for (tree a = TYPE_ARG_TYPES (type);
       a && TREE_VALUE (a) != void_type_node; a = TREE_CHAIN (a))
    args.safe_push (mgd (TREE_VALUE (a)));
  tree nt = varargs
	    ? build_varargs_function_type_array (mgd (TREE_TYPE (type)),
						 args.length (),
						 args.address ())
	    : build_function_type_array (mgd (TREE_TYPE (type)),
					 args.length (), args.address ());
  args.release ();
  return build_type_attribute_qual_variant (nt, TYPE_ATTRIBUTES (type),
					    TYPE_QUALS (type));
}

/* Names that a system header declares (raw pointers) but the program itself
   defines in Managed C, so every site must use the managed ABI.  These are
   the openbsd-compat libc substitutes that collide with glibc headers; the
   program links its own body (configure left HAVE_*=undef), yet <resolv.h>
   still declares b64_ntop/b64_pton.  Fail-stop either way: a name wrongly
   here that is really host would want a managed body that does not exist and
   fail to link, never corrupt silently.  (task #59, resolving the OpenSSH
   base64 ABI split.)  */
static bool
managed_override_name_p (tree id)
{
  if (!id)
    return false;
  static const char *const names[] = {
    "b64_ntop", "__b64_ntop", "b64_pton", "__b64_pton",
  };
  const char *s = IDENTIFIER_POINTER (id);
  for (size_t i = 0; i < ARRAY_SIZE (names); i++)
    if (strcmp (s, names[i]) == 0)
      return true;
  return tzd_managed_override_p (s);
}

/* FNDECL is called from an asm ("call %c[func]" with [func] "i" (func):
   the kernel's call_on_irqstack): it gets the native calling convention,
   since the asm passes raw pointers in registers.  A static function not
   defined yet is switched here (its definition follows in this unit);
   any other with pointers in its signature must be in the native list
   (-ftzd-native), or its callers and its definition would disagree.  */
static void
native_from_asm (tree fndecl, location_t loc)
{
  tree t = TREE_TYPE (fndecl);
  bool ptrs = TZD_MPTR_P (TREE_TYPE (t));
  for (tree a = TYPE_ARG_TYPES (t); a; a = TREE_CHAIN (a))
    if (TZD_MPTR_P (TREE_VALUE (a)))
      ptrs = true;
  if (!ptrs || native_list_p (DECL_NAME (fndecl)))
    return;
  if (native_name_p (DECL_NAME (fndecl)) && !TREE_PUBLIC (fndecl))
    return;   /* found by asm_prescan: native since its declaration */
  if (TREE_PUBLIC (fndecl) || DECL_INITIAL (fndecl))
    {
      error_at (loc, "%qD is called from %<asm%> with pointer arguments: "
		"it must be in the native function list (%<-ftzd-native%>)",
		fndecl);
      return;
    }
  native_names->add (DECL_NAME (fndecl));
  TREE_TYPE (fndecl) = native_fn_type (t);
}

/* Functions an asm template calls by name ("call __alt_reloc_selftest",
   "jmp foo"): they get the native calling convention too
   (native_from_asm).  Indirect calls ("call *%[x]") and local labels are
   not names; a name that is not a function declared here is left
   alone.  */
static void
native_from_asm_template (tree str, location_t loc)
{
  if (!str || TREE_CODE (str) != STRING_CST)
    return;
  const char *s = TREE_STRING_POINTER (str);
  const char *end = s + TREE_STRING_LENGTH (str);
  for (const char *c = s; c < end; c++)
    {
      int kw = 0;
      if (end - c > 4 && strncmp (c, "call", 4) == 0)
	kw = 4;
      else if (end - c > 3 && strncmp (c, "jmp", 3) == 0)
	kw = 3;
      if (!kw || (c > s && (ISALNUM (c[-1]) || c[-1] == '_')))
	continue;
      const char *p = c + kw;
      if (*p == 'q' || *p == 'l')
	p++;
      if (*p != ' ' && *p != '\t')
	continue;
      while (p < end && (*p == ' ' || *p == '\t'))
	p++;
      if (p >= end || !(ISALPHA (*p) || *p == '_'))
	continue;
      const char *q = p;
      while (q < end && (ISALNUM (*q) || *q == '_'))
	q++;
      if (q < end && (*q == '%' || *q == '.'))
	continue;		/* built from an operand, or a local name */
      tree id = get_identifier_with_length (p, q - p);
      tree d = tzd_lookup (id);
      if (d && TREE_CODE (d) == FUNCTION_DECL)
	native_from_asm (d, loc);
      c = q - 1;
    }
}

/* Load the managed pointer stored in slot lvalue L (8 bytes in memory):
   libc2d checks the shadow entry against the stored value (a pointer
   overwritten by a non-pointer store traps) and returns the value record
   of type REC.  */
/* Name of record/union type T (its tag, or the typedef naming it).  */
static const char *
type_name_str (tree t)
{
  tree n = TYPE_NAME (t);
  if (!n)
    n = TYPE_NAME (TYPE_MAIN_VARIANT (t));
  if (n && TREE_CODE (n) == TYPE_DECL)
    n = DECL_NAME (n);
  return n && TREE_CODE (n) == IDENTIFIER_NODE ? IDENTIFIER_POINTER (n) : NULL;
}

/* Is L a union member named by `#pragma tzd lazy_pointer` (task #70)?
   Both the type as written (a typedef) and its main variant (the tag)
   are tried.  */
static bool
lazy_slot_p (tree l)
{
  if (TREE_CODE (l) != COMPONENT_REF)
    return false;
  tree u = TREE_TYPE (TREE_OPERAND (l, 0)), f = TREE_OPERAND (l, 1);
  if (TREE_CODE (u) != UNION_TYPE || !DECL_NAME (f))
    return false;
  const char *fn = IDENTIFIER_POINTER (DECL_NAME (f));
  const char *n1 = type_name_str (u), *n2 = type_name_str (TYPE_MAIN_VARIANT (u));
  return (n1 && tzd_lazy_pointer_p (n1, fn)) || (n2 && tzd_lazy_pointer_p (n2, fn));
}

/* `#pragma tzd fnptr_holds` (task #70).  The list for member reference L
   when L is such a function-pointer member, else NULL.  */
static const char *const *fnptr_holds_field (tree st, tree f);

static const char *const *
fnptr_holds_list (tree l)
{
  if (!l || TREE_CODE (l) != COMPONENT_REF)
    return NULL;
  return fnptr_holds_field (TREE_TYPE (TREE_OPERAND (l, 0)),
			    TREE_OPERAND (l, 1));
}

/* Does an object of TYPE contain a fnptr_holds member (static tables of
   them need their initializers registered)?  */
static bool
has_fnholds_p (tree type)
{
  if (!tzd_fnptr_holds_any ())
    return false;
  type = TYPE_MAIN_VARIANT (type);
  if (TREE_CODE (type) == ARRAY_TYPE)
    return has_fnholds_p (TREE_TYPE (type));
  if (RECORD_OR_UNION_TYPE_P (type) && !TZD_MREC_P (type))
    for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
      if (TREE_CODE (f) == FIELD_DECL
	  && (fnptr_holds_field (type, f) || has_fnholds_p (TREE_TYPE (f))))
	return true;
  return false;
}

static const char *const *
fnptr_holds_field (tree st, tree f)
{
  tree ft = TREE_TYPE (f);
  if (!RECORD_OR_UNION_TYPE_P (st) || !DECL_NAME (f)
      || TREE_CODE (ft) != POINTER_TYPE
      || TREE_CODE (TREE_TYPE (ft)) != FUNCTION_TYPE)
    return NULL;
  const char *fn = IDENTIFIER_POINTER (DECL_NAME (f));
  const char *n1 = type_name_str (st), *n2 = type_name_str (TYPE_MAIN_VARIANT (st));
  const char *const *r = n1 ? tzd_fnptr_holds (n1, fn) : NULL;
  if (!r && n2)
    r = tzd_fnptr_holds (n2, fn);
  return r;
}

/* Is T a plain char (or void): a byte view of any object.  */
static bool
byte_view_type_p (tree t)
{
  t = TYPE_MAIN_VARIANT (t);
  return VOID_TYPE_P (t) || t == char_type_node || t == signed_char_type_node
	 || t == unsigned_char_type_node;
}

/* Is pointee type T one of LIST?  Struct tags and typedef names, and
   `char` for the char types.  */
static bool
fnptr_holds_allows (const char *const *list, tree t)
{
  const char *n1 = type_name_str (t), *n2 = type_name_str (TYPE_MAIN_VARIANT (t));
  bool ch = byte_view_type_p (t) && !VOID_TYPE_P (t);
  for (; *list; list++)
    if ((n1 && strcmp (*list, n1) == 0) || (n2 && strcmp (*list, n2) == 0)
	|| (ch && strcmp (*list, "char") == 0))
      return true;
  return false;
}

/* The type id recorded with a data pointer of pointee type T (never 0 or
   1: libc2d keeps it in the entry's `live`).  The struct tag (else the
   typedef name), or "char".  */
static unsigned long
fnptr_holds_id (tree t)
{
  const char *n = byte_view_type_p (t) ? "char"
		  : type_name_str (TYPE_MAIN_VARIANT (t));
  if (!n)
    n = type_name_str (t);
  if (!n)
    n = "?";
  unsigned long h = 1469598103934665603UL;
  for (; *n; n++)
    {
      h ^= (unsigned char) *n;
      h *= 1099511628211UL;
    }
  return h | 2;
}

static tree
slot_load (tree l, tree rec, location_t loc)
{
  init_shadow_decls ();
  mark_addressable_tree (l);
  tree addr = build_fold_addr_expr_with_type_loc (loc, l,
						  build_pointer_type
						    (TREE_TYPE (l)));
  tree c = build_call_expr_loc (loc, lazy_slot_p (l) ? tzd_sh_load_lazy_decl
				: tzd_sh_load_decl, 1,
				fold_convert (const_ptr_type_node, addr));
  return build1 (VIEW_CONVERT_EXPR, TYPE_MAIN_VARIANT (rec), c);
}

/* Store value record V into slot lvalue L (and its shadow entry).  */
static tree
slot_store (tree l, tree v, location_t loc)
{
  init_shadow_decls ();
  mark_addressable_tree (l);
  tree addr = build_fold_addr_expr_with_type_loc (loc, l,
						  build_pointer_type
						    (TREE_TYPE (l)));
  tree vr = tzd_mrec_type (void_type_node);
  return build_call_expr_loc (loc, tzd_sh_store_decl, 2,
			      fold_convert (ptr_type_node, addr),
			      build1 (VIEW_CONVERT_EXPR, vr, v));
}

/* Copy the shadow entries of N bytes at SRC to DST (both native addresses)
   after an aggregate copy.  */
static tree
shadow_copy (tree dst, tree src, tree n, location_t loc)
{
  init_shadow_decls ();
  return build_call_expr_loc (loc, tzd_sh_copy_decl, 3,
			      fold_convert (ptr_type_node, dst),
			      fold_convert (const_ptr_type_node, src),
			      fold_convert (size_type_node, n));
}

/* Does TYPE contain managed pointer slots (so copies must carry shadow
   entries)?  */
static bool
has_slots_p (tree type)
{
  type = TYPE_MAIN_VARIANT (type);
  if (TZD_MSLOT_P (type))
    return true;
  if (TREE_CODE (type) == ARRAY_TYPE)
    return has_slots_p (TREE_TYPE (type));
  if (RECORD_OR_UNION_TYPE_P (type) && !TZD_MREC_P (type))
    for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
      if (TREE_CODE (f) == FIELD_DECL && has_slots_p (TREE_TYPE (f)))
	return true;
  return false;
}

/* ---- Structs holding pointers passed or returned by value ----
   TK 2026-11-09: allowed for now as (b); the planned final rule is a
   compile error asking for TzD-style code (task #46).  Such a value
   cannot travel in registers or a plain copy: its pointers' bounds live
   in the bounds table keyed by address.  So a function whose type has one
   (its "language" type, used for all checking) gets a lowered real type:
   the struct parameter becomes a hidden pointer to a caller's temporary
   copy, and a struct result becomes a hidden first parameter pointing to
   the caller's result object.  Every copy is a bounds-table copy
   (aggregate_copy).  Public functions get the symbol suffix __tzd_byval,
   so a caller compiled without cc1tzd (other ABI) fails to link instead of
   passing the struct the C way.  */
static bool
sv_type_p (tree t)
{
  return t && RECORD_OR_UNION_TYPE_P (t) && !TZD_MREC_P (t)
	 && COMPLETE_TYPE_P (t) && has_slots_p (t);
}

static bool
sv_fntype_p (tree ft)
{
  if (!ft || TREE_CODE (ft) != FUNCTION_TYPE)
    return false;
  if (sv_type_p (TREE_TYPE (ft)))
    return true;
  for (tree a = TYPE_ARG_TYPES (ft); a; a = TREE_CHAIN (a))
    if (sv_type_p (TREE_VALUE (a)))
      return true;
  return false;
}

static GTY(()) vec<tree, va_gc> *sv_types;   /* (language, lowered) pairs */
static GTY(()) vec<tree, va_gc> *sv_decls;   /* (fndecl, language type) */

static tree
sv_lower_type (tree ft)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (sv_types); i += 2)
    if ((*sv_types)[i] == ft)
      return (*sv_types)[i + 1];
  vec<tree> args = vNULL;
  tree ret = TREE_TYPE (ft);
  if (sv_type_p (ret))
    {
      args.safe_push (build_pointer_type (ret));
      ret = void_type_node;
    }
  for (tree a = TYPE_ARG_TYPES (ft); a && TREE_VALUE (a) != void_type_node;
       a = TREE_CHAIN (a))
    args.safe_push (sv_type_p (TREE_VALUE (a))
		    ? build_pointer_type (TREE_VALUE (a)) : TREE_VALUE (a));
  tree nt = stdarg_p (ft)
	    ? build_varargs_function_type_array (ret, args.length (),
						 args.address ())
	    : build_function_type_array (ret, args.length (), args.address ());
  args.release ();
  vec_safe_push (sv_types, ft);
  vec_safe_push (sv_types, nt);
  return nt;
}

/* The language type of function FN (its declared type).  */
static tree
fn_lang_type (tree fn)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (sv_decls); i += 2)
    if ((*sv_decls)[i] == fn)
      return (*sv_decls)[i + 1];
  return TREE_TYPE (fn);
}

/* Give FN declared type LANG: its real type is the lowered one when LANG
   passes or returns such a struct.  */
static void
sv_set_fn_type (tree fn, tree lang)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (sv_decls); i += 2)
    if ((*sv_decls)[i] == fn)
      {
	(*sv_decls)[i + 1] = lang;
	TREE_TYPE (fn) = sv_fntype_p (lang) ? sv_lower_type (lang) : lang;
	return;
      }
  if (!sv_fntype_p (lang))
    {
      TREE_TYPE (fn) = lang;
      return;
    }
  vec_safe_push (sv_decls, fn);
  vec_safe_push (sv_decls, lang);
  TREE_TYPE (fn) = sv_lower_type (lang);
}

/* va_list (on x86-64 an array of __va_list_tag): stays native.  */
static bool
va_list_p (tree t)
{
  if (t == NULL_TREE || error_p (t) || va_list_type_node == NULL_TREE)
    return false;
  if (TYPE_MAIN_VARIANT (t) == TYPE_MAIN_VARIANT (va_list_type_node))
    return true;
  return (TREE_CODE (t) == ARRAY_TYPE
	  && TREE_CODE (va_list_type_node) == ARRAY_TYPE
	  && TYPE_MAIN_VARIANT (TREE_TYPE (t))
	     == TYPE_MAIN_VARIANT (TREE_TYPE (va_list_type_node)));
}

/* (void *)0 and friends: a managed null pointer constant.  It may also
   become a native null (function pointers, raw builtin parameters).  */
static bool
null_mptr_p (const ex &e)
{
  if (!TZD_MPTR_P (e.type))
    return false;
  if (e.t == NULL_TREE)
    return e.pa && e.pb && integer_zerop (e.pa) && integer_zerop (e.pb);
  if (TREE_CODE (e.t) == CONSTRUCTOR)
    {
      unsigned i;
      tree v;
      FOR_EACH_CONSTRUCTOR_VALUE (CONSTRUCTOR_ELTS (e.t), i, v)
	if (!integer_zerop (v))
	  return false;
      return true;
    }
  return false;
}

static void
mark_addressable (tree t)
{
  while (handled_component_p (t))
    t = TREE_OPERAND (t, 0);
  if (DECL_P (t))
    TREE_ADDRESSABLE (t) = 1;
}

/* The three parts of a managed pointer value.  */
static void
parts (ex &e)
{
  if (e.t == NULL_TREE)
    return;
  if (e.lv && TZD_MSLOT_P (TREE_TYPE (e.t)))
    {
      /* A slot in memory: load it (callers normally go through rvalue).  */
      e.t = slot_load (e.t, mrec (e.type), EXPR_LOCATION (e.t));
      e.lv = false;
    }
  tree w = e.t;
  if (e.lv)
    w = stabilize_reference (w);
  else
    w = save_expr (w);
  tree type = e.type;
  e.pa = build3 (COMPONENT_REF, mp_native (type), w, mp_field (type, 0),
		 NULL_TREE);
  e.pb = build3 (COMPONENT_REF, ptr_type_node, w, mp_field (type, 1),
		 NULL_TREE);
  e.ps = build3 (COMPONENT_REF, size_type_node, w, mp_field (type, 2),
		 NULL_TREE);
  e.t = NULL_TREE;
  e.lv = false;
}

/* Record-typed tree for a managed pointer value.  */
static tree
whole (const ex &e)
{
  if (e.t)
    return e.t;
  tree type = mrec (e.type);
  vec<constructor_elt, va_gc> *v = NULL;
  CONSTRUCTOR_APPEND_ELT (v, mp_field (type, 0),
			  fold_convert (mp_native (type), e.pa));
  CONSTRUCTOR_APPEND_ELT (v, mp_field (type, 1), e.pb);
  CONSTRUCTOR_APPEND_ELT (v, mp_field (type, 2), e.ps);
  tree c = build_constructor (type, v);
  if (TREE_CONSTANT (c))
    TREE_STATIC (c) = 1;
  return c;
}

/* Does this base yield a function-call rvalue (so it is not an lvalue and
   cannot be addressed directly)?  cc1tzd wraps a call that has managed
   pointer arguments in BIND_EXPR/COMPOUND_EXPR (argument temporaries), so
   look through those to the result; a COMPOUND_EXPR whose result is an
   lvalue (a bounds check before an lvalue) is NOT a call rvalue.  */
static bool
call_rvalue_p (tree t)
{
  while (t)
    {
      if (TREE_CODE (t) == CALL_EXPR)
	return true;
      if (TREE_CODE (t) == COMPOUND_EXPR)
	{ t = TREE_OPERAND (t, 1); continue; }
      if (TREE_CODE (t) == BIND_EXPR)
	{ t = BIND_EXPR_BODY (t); continue; }
      return false;
    }
  return false;
}

/* Native address of the first element of array lvalue T.  */
static tree
array_first_addr (tree t, location_t loc)
{
  tree elt = TREE_TYPE (TREE_TYPE (t));
  tree dom = TYPE_DOMAIN (TREE_TYPE (t));
  tree lo = dom && TYPE_MIN_VALUE (dom) ? TYPE_MIN_VALUE (dom)
	    : size_zero_node;
  /* An array member of a struct rvalue -- f().arr, where f returns a struct
     by value -- has a non-addressable base (the CALL_EXPR), so taking its
     address would build a malformed tree.  Materialize the rvalue in a
     temporary (a TARGET_EXPR) first, so the member and its address are
     well-formed.  */
  {
    tree *bp = &t;
    while (handled_component_p (*bp))
      bp = &TREE_OPERAND (*bp, 0);
    tree base = *bp;
    if (base && current_function_decl
	&& call_rvalue_p (base)
	&& AGGREGATE_TYPE_P (TREE_TYPE (base)))
      {
	tree tmp = build_decl (loc, VAR_DECL, NULL_TREE, TREE_TYPE (base));
	DECL_ARTIFICIAL (tmp) = 1;
	DECL_IGNORED_P (tmp) = 1;
	DECL_CONTEXT (tmp) = current_function_decl;
	*bp = build4 (TARGET_EXPR, TREE_TYPE (base), tmp, base,
		      NULL_TREE, NULL_TREE);
      }
  }
  mark_addressable (t);
  tree ref = build4 (ARRAY_REF, elt, t, lo, NULL_TREE, NULL_TREE);
  return build_fold_addr_expr_with_type_loc (loc, ref,
					     build_pointer_type (elt));
}

/* Array decay: managed pointer covering the whole array.  */
/* ---- Bounds rules for pointers coming from the host (task #1, #9) ----

   A raw pointer produced by the host (a host function's return value, a
   field of a host struct, a host variable, or an element reached through
   one of those) becomes a managed pointer only through a rule that says
   what it points to.  There is no default: a raw pointer without a rule is
   a compile error (TK 2026-11-06, security first).

   Kinds:
     's'  NUL-terminated string: [p, p + strlen (p) + 1)
     'w'  NUL-terminated wide string: [p, p + (wcslen (p) + 1) * 4)
     'o'  A objects of the pointee type (A = 0 means 1)
     'x'  opaque handle (FILE *, DIR *, dlopen): no bytes reachable
     'g'  points into argument A: that argument's bounds (else trap)
     'c'  like 'g', or a host string when outside it (getcwd (NULL, 0))
     'l'  A bytes, A taken from argument number ARG (mmap length)
     'm'  shmat: the segment size of the id in argument ARG
     'v'  NULL-terminated vector of pointers (h_aliases, gr_mem)
     't'  table indexed from -A, B elements (ctype tables)
   An element rule (EKIND, EA, EB) applies to raw pointers read through
   the result (char ** vectors, pointer-to-table).  */
struct host_ptr_rule
{
  const char *name;   /* function, variable, or "struct.field" */
  char kind;
  int a, b;
  char ekind;
  int ea, eb;
};

static const host_ptr_rule host_ptr_rules[] = {
  /* Functions returning strings.  */
  { "getenv", 's', 0, 0, 0, 0, 0 }, { "strerror", 's', 0, 0, 0, 0, 0 },
  { "secure_getenv", 's', 0, 0, 0, 0, 0 }, { "gai_strerror", 's', 0, 0, 0, 0, 0 },
  { "nl_langinfo", 's', 0, 0, 0, 0, 0 },
  { "crypt", 's', 0, 0, 0, 0, 0 }, { "ctime", 's', 0, 0, 0, 0, 0 },
  { "inet_ntoa", 's', 0, 0, 0, 0, 0 }, { "dlerror", 's', 0, 0, 0, 0, 0 },
  { "getpass", 's', 0, 0, 0, 0, 0 },
  { "hstrerror", 's', 0, 0, 0, 0, 0 }, { "strsignal", 's', 0, 0, 0, 0, 0 },
  { "setlocale", 's', 0, 0, 0, 0, 0 }, { "getlogin", 's', 0, 0, 0, 0, 0 },
  { "ttyname", 's', 0, 0, 0, 0, 0 }, { "zlibVersion", 's', 0, 0, 0, 0, 0 },
  /* Functions returning one object of the declared type.  */
  { "gmtime", 'o', 1, 0, 0, 0, 0 }, { "localtime", 'o', 1, 0, 0, 0, 0 },
  { "getpwnam", 'o', 1, 0, 0, 0, 0 }, { "getpwuid", 'o', 1, 0, 0, 0, 0 },
  { "getpwent", 'o', 1, 0, 0, 0, 0 }, { "getspnam", 'o', 1, 0, 0, 0, 0 },
  { "__res_state", 'o', 1, 0, 0, 0, 0 },
  { "getgrnam", 'o', 1, 0, 0, 0, 0 }, { "getgrgid", 'o', 1, 0, 0, 0, 0 },
  { "gethostbyname", 'o', 1, 0, 0, 0, 0 },
  { "gethostbyaddr", 'o', 1, 0, 0, 0, 0 },
  { "readdir", 'o', 1, 0, 0, 0, 0 },
  { "__errno_location", 'o', 1, 0, 0, 0, 0 },
  { "__h_errno_location", 'o', 1, 0, 0, 0, 0 },
  { "getservbyname", 'o', 1, 0, 0, 0, 0 },
  { "getservbyport", 'o', 1, 0, 0, 0, 0 },
  { "getmntent", 'o', 1, 0, 0, 0, 0 },
  { "getgrent", 'o', 1, 0, 0, 0, 0 }, { "getservent", 'o', 1, 0, 0, 0, 0 },
  { "localeconv", 'o', 1, 0, 0, 0, 0 },
  /* glibc ctype: a pointer to the table pointer; the table is indexed
     from -128 to 255.  */
  { "__ctype_b_loc", 'o', 1, 0, 't', 128, 384 },
  { "__ctype_tolower_loc", 'o', 1, 0, 't', 128, 384 },
  { "__ctype_toupper_loc", 'o', 1, 0, 't', 128, 384 },
  /* Opaque handles.  */
  { "fopen", 'x', 0, 0, 0, 0, 0 }, { "fdopen", 'x', 0, 0, 0, 0, 0 },
  { "setmntent", 'x', 0, 0, 0, 0, 0 },
  { "freopen", 'x', 0, 0, 0, 0, 0 }, { "opendir", 'x', 0, 0, 0, 0, 0 },
  { "dlopen", 'x', 0, 0, 0, 0, 0 }, { "dlsym", 'x', 0, 0, 0, 0, 0 },
  { "popen", 'x', 0, 0, 0, 0, 0 }, { "tmpfile", 'x', 0, 0, 0, 0, 0 },
  { "gzopen", 'x', 0, 0, 0, 0, 0 }, { "gzdopen", 'x', 0, 0, 0, 0, 0 },
  { "iconv_open", 'x', 0, 0, 0, 0, 0 },
  /* GCC builtins giving stack and code addresses: compared, never read
     through (kernel stack walking); reading through one traps.  */
  { "__builtin_frame_address", 'x', 0, 0, 0, 0, 0 },
  { "__builtin_return_address", 'x', 0, 0, 0, 0, 0 },
  { "__builtin_extract_return_addr", 'x', 0, 0, 0, 0, 0 },
  { "__builtin_thread_pointer", 'x', 0, 0, 0, 0, 0 },
  /* Fields of host structs (read when the host wrote them).  */
  { "hostent.h_name", 's', 0, 0, 0, 0, 0 },
  { "hostent.h_aliases", 'v', 0, 0, 's', 0, 0 },
  /* Addresses: 4 bytes (AF_INET, what gethostbyname returns here); a
     16-byte AF_INET6 address read through it traps rather than being
     guessed.  */
  { "hostent.h_addr_list", 'v', 0, 0, 'o', 4, 0 },
  { "passwd.pw_name", 's', 0, 0, 0, 0, 0 },
  { "passwd.pw_passwd", 's', 0, 0, 0, 0, 0 },
  { "passwd.pw_gecos", 's', 0, 0, 0, 0, 0 },
  { "passwd.pw_dir", 's', 0, 0, 0, 0, 0 },
  { "passwd.pw_shell", 's', 0, 0, 0, 0, 0 },
  { "spwd.sp_namp", 's', 0, 0, 0, 0, 0 },
  { "spwd.sp_pwdp", 's', 0, 0, 0, 0, 0 },
  { "group.gr_name", 's', 0, 0, 0, 0, 0 },
  { "group.gr_passwd", 's', 0, 0, 0, 0, 0 },
  { "group.gr_mem", 'v', 0, 0, 's', 0, 0 },
  { "tm.tm_zone", 's', 0, 0, 0, 0, 0 },
  { "option.name", 's', 0, 0, 0, 0, 0 },
  { "option.flag", 'o', 1, 0, 0, 0, 0 },
  { "servent.s_name", 's', 0, 0, 0, 0, 0 },
  { "servent.s_aliases", 'v', 0, 0, 's', 0, 0 },
  { "servent.s_proto", 's', 0, 0, 0, 0, 0 },
  /* getaddrinfo's result list.  ai_next is one more node, ai_canonname a
     NUL string.  ai_addr points at one struct sockaddr (AF_INET, 16 bytes,
     what a 127.0.0.1 listen resolves to); a larger AF_INET6 sockaddr read
     through it traps rather than being guessed, like hostent.h_addr_list.  */
  { "addrinfo.ai_next", 'o', 1, 0, 0, 0, 0 },
  { "addrinfo.ai_canonname", 's', 0, 0, 0, 0, 0 },
  { "addrinfo.ai_addr", 'o', 1, 0, 0, 0, 0 },
  { "mntent.mnt_fsname", 's', 0, 0, 0, 0, 0 },
  { "mntent.mnt_dir", 's', 0, 0, 0, 0, 0 },
  { "mntent.mnt_type", 's', 0, 0, 0, 0, 0 },
  { "mntent.mnt_opts", 's', 0, 0, 0, 0, 0 },
  /* struct lconv (localeconv): the strings bash reads.  */
  { "lconv.decimal_point", 's', 0, 0, 0, 0, 0 },
  { "lconv.thousands_sep", 's', 0, 0, 0, 0, 0 },
  { "lconv.grouping", 's', 0, 0, 0, 0, 0 },
  /* Results inside an argument / sized by an argument.  */
  { "fgets", 'g', 0, 0, 0, 0, 0 },
  { "fgets_unlocked", 'g', 0, 0, 0, 0, 0 },
  { "gzgets", 'g', 1, 0, 0, 0, 0 },
  /* Results in a caller's struct.  */
  { "localtime_r", 'g', 1, 0, 0, 0, 0 }, { "gmtime_r", 'g', 1, 0, 0, 0, 0 },
  /* Pointers into the first argument: where parsing stopped (strptime),
     the directory part written into the path or a static "." (dirname);
     the destination (inet_ntop, sized by its fourth argument).  */
  { "strptime", 'g', 0, 0, 0, 0, 0 }, { "dirname", 'c', 0, 0, 0, 0, 0 },
  { "__xpg_basename", 'c', 0, 0, 0, 0, 0 },
  { "mkdtemp", 'g', 0, 0, 0, 0, 0 }, { "memmem", 'g', 0, 0, 0, 0, 0 },
  { "mktemp", 'g', 0, 0, 0, 0, 0 },
  /* Wide-character searches: a pointer into the searched string.  */
  { "wcschr", 'g', 0, 0, 0, 0, 0 }, { "wcsrchr", 'g', 0, 0, 0, 0, 0 },
  { "wmemchr", 'g', 0, 0, 0, 0, 0 },
  { "inet_ntop", 'g', 2, 0, 0, 0, 0 },
  { "getmntent_r", 'g', 1, 0, 0, 0, 0 },
  /* One control message header inside msg_control (its data is reached
     through CMSG_DATA from it; past the header it traps).  */
  { "__cmsg_nxthdr", 'o', 1, 0, 0, 0, 0 },
  { "getcwd", 'c', 0, 0, 0, 0, 0 },
  /* Directory and locale handles (opaque), strings from the host
     (coreutils 9.11, task #71).  strerror_r (GNU) returns BUF or a
     static string.  */
  { "fdopendir", 'x', 0, 0, 0, 0, 0 },
  { "newlocale", 'x', 0, 0, 0, 0, 0 }, { "uselocale", 'x', 0, 0, 0, 0, 0 },
  { "nl_langinfo_l", 's', 0, 0, 0, 0, 0 },
  { "strerrorname_np", 's', 0, 0, 0, 0, 0 },
  { "strerror_r", 'c', 1, 0, 0, 0, 0 },
  { "hasmntopt", 's', 0, 0, 0, 0, 0 },
  { "getutxent", 'o', 1, 0, 0, 0, 0 },
  /* ctermid (buf) fills buf; ctermid (NULL) returns a static string.  */
  { "ctermid", 'c', 0, 0, 0, 0, 0 },
  { "strdup", 's', 0, 0, 0, 0, 0 }, { "strndup", 's', 0, 0, 0, 0, 0 },
  { "wcsdup", 'w', 0, 0, 0, 0, 0 },
  { "mmap", 'l', 1, 0, 0, 0, 0 },
  { "shmat", 'm', 0, 0, 0, 0, 0 },
  /* Host variables and arrays ("name[]": count, element rule).  */
  { "optarg", 's', 0, 0, 0, 0, 0 },
  { "__progname", 's', 0, 0, 0, 0, 0 },
  /* The environment: a NULL-terminated vector of strings.  */
  { "environ", 'v', 0, 0, 's', 0, 0 }, { "__environ", 'v', 0, 0, 's', 0, 0 },
  { "_sys_siglist[]", 'o', 65, 0, 's', 0, 0 },     /* NSIG entries */
  { "stdin", 'x', 0, 0, 0, 0, 0 }, { "stdout", 'x', 0, 0, 0, 0, 0 },
  { "stderr", 'x', 0, 0, 0, 0, 0 },
};

static const host_ptr_rule *
host_ptr_rule_for (const char *name)
{
  for (size_t i = 0; i < ARRAY_SIZE (host_ptr_rules); i++)
    if (strcmp (name, host_ptr_rules[i].name) == 0)
      return &host_ptr_rules[i];
  return NULL;
}

/* Raw pointer type RAW marked with an element rule (kind, a, b): what
   a raw pointer read through a ruled result means.  TYPE_LANG_FLAG_2,
   rule in TYPE_LANG_SLOT_1 as a TREE_VEC of three INTEGER_CSTs.  */
#define TZD_HRULE_P(TYPE) \
  (TREE_CODE (TYPE) == POINTER_TYPE && TYPE_LANG_FLAG_2 (TYPE))

static GTY(()) vec<tree, va_gc> *hrule_types;   /* (raw, vec, marked) */

static tree
hrule_type (tree raw, char kind, int a, int b)
{
  tree m = TYPE_MAIN_VARIANT (raw);
  for (unsigned i = 0; i + 2 < vec_safe_length (hrule_types); i += 3)
    {
      tree v = (*hrule_types)[i + 1];
      if ((*hrule_types)[i] == m
	  && tree_to_shwi (TREE_VEC_ELT (v, 0)) == kind
	  && tree_to_shwi (TREE_VEC_ELT (v, 1)) == a
	  && tree_to_shwi (TREE_VEC_ELT (v, 2)) == b)
	{
	  tree t = (*hrule_types)[i + 2];
	  return TYPE_QUALS (raw) ? build_qualified_type (t, TYPE_QUALS (raw))
				  : t;
	}
    }
  tree v = make_tree_vec (3);
  TREE_VEC_ELT (v, 0) = build_int_cst (integer_type_node, kind);
  TREE_VEC_ELT (v, 1) = build_int_cst (integer_type_node, a);
  TREE_VEC_ELT (v, 2) = build_int_cst (integer_type_node, b);
  tree t = build_distinct_type_copy (m);
  TYPE_LANG_FLAG_2 (t) = 1;
  TYPE_LANG_SLOT_1 (t) = v;
  TYPE_CANONICAL (t) = TYPE_CANONICAL (m);
  vec_safe_push (hrule_types, m);
  vec_safe_push (hrule_types, v);
  vec_safe_push (hrule_types, t);
  return TYPE_QUALS (raw) ? build_qualified_type (t, TYPE_QUALS (raw)) : t;
}

/* __tzd_size.NAME: the byte size of public array NAME, published by the
   unit that defines it (task #1: an extern T x[] has no default bounds).  */
static GTY(()) vec<tree, va_gc> *size_syms;   /* (name, decl) */

static tree
array_size_decl (tree id, bool define)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (size_syms); i += 2)
    if ((*size_syms)[i] == id)
      return (*size_syms)[i + 1];
  char nm[300];
  snprintf (nm, sizeof nm, "__tzd_size.%s", IDENTIFIER_POINTER (id));
  tree d = build_decl (BUILTINS_LOCATION, VAR_DECL, get_identifier (nm),
		       build_qualified_type (size_type_node, TYPE_QUAL_CONST));
  TREE_PUBLIC (d) = 1;
  TREE_READONLY (d) = 1;
  DECL_ARTIFICIAL (d) = 1;
  TREE_STATIC (d) = define;
  DECL_EXTERNAL (d) = !define;
  vec_safe_push (size_syms, id);
  vec_safe_push (size_syms, d);
  return d;
}

static tree
array_size_symbol (tree var)
{
  tree d = array_size_decl (DECL_NAME (var), false);
  /* A file-scope static array declared before its size is known
     (kernel/cpu.c's static struct cpuhp_step cpuhp_hp_states[];): the
     size symbol is local to this unit, defined at the end of it.  */
  if (!TREE_PUBLIC (var))
    {
      TREE_PUBLIC (d) = 0;
      /* The late registrations may be its only user.  */
      DECL_PRESERVE_P (d) = 1;
    }
  TREE_USED (d) = 1;
  return d;
}

/* The size symbol already made for NAME, or NULL.  */
static tree
find_size_decl (tree id)
{
  for (unsigned i = 0; i + 1 < vec_safe_length (size_syms); i += 2)
    if ((*size_syms)[i] == id)
      return (*size_syms)[i + 1];
  return NULL_TREE;
}

/* F is the last FIELD_DECL of its struct.  */
static bool
last_field_p (tree f)
{
  for (tree r = DECL_CHAIN (f); r; r = DECL_CHAIN (r))
    if (TREE_CODE (r) == FIELD_DECL)
      return false;
  return true;
}

/* Array type of unknown size, or of 0 or 1 elements (the flexible-array
   idiom).  */
static bool
fam_array_type_p (tree t)
{
  if (TREE_CODE (t) != ARRAY_TYPE)
    return false;
  tree dom = TYPE_DOMAIN (t);
  return (dom == NULL_TREE || TYPE_MAX_VALUE (dom) == NULL_TREE
	  /* [0]: the bound is sizetype -1, which is unsigned.  */
	  || (TYPE_SIZE_UNIT (t) && integer_zerop (TYPE_SIZE_UNIT (t)))
	  || (TREE_CODE (TYPE_MAX_VALUE (dom)) == INTEGER_CST
	      && tree_int_cst_sgn (TYPE_MAX_VALUE (dom)) <= 0));
}

/* Struct TYPE ends in a flexible array, directly or through its last
   member.  */
static bool
ends_in_fam_p (tree type)
{
  if (TREE_CODE (type) == UNION_TYPE)
    {
      /* A union of flexible arrays (rpl.h's segments: DECLARE_FLEX_ARRAY
	 members) reaches the end of the allocation through any of them.  */
      for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
	if (TREE_CODE (f) == FIELD_DECL
	    && (fam_array_type_p (TREE_TYPE (f))
		|| ends_in_fam_p (TREE_TYPE (f))))
	  return true;
      return false;
    }
  if (TREE_CODE (type) != RECORD_TYPE)
    return false;
  tree last = NULL_TREE;
  for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) == FIELD_DECL)
      last = f;
  if (!last)
    return false;
  return fam_array_type_p (TREE_TYPE (last))
	 || ends_in_fam_p (TREE_TYPE (last));
}

/* T is a trailing flexible array member reached through a pointer (no
   allocation bounds known here).  Walk out through anonymous members to
   the named object that holds it (sockaddr's DECLARE_FLEX_ARRAY sa_data
   in an anonymous union with sa_data_min[14]); when that object's type
   covers bytes past the array's start, its bounds are the array's.  */
static bool
fam_container_bounds (tree t, tree *base, tree *size)
{
  tree off = size_zero_node;
  while (TREE_CODE (t) == COMPONENT_REF)
    {
      tree f = TREE_OPERAND (t, 1);
      off = size_binop (PLUS_EXPR, off, byte_position (f));
      t = TREE_OPERAND (t, 0);
      if (TREE_CODE (t) != COMPONENT_REF
	  || DECL_NAME (TREE_OPERAND (t, 1)) != NULL_TREE)
	break;
    }
  tree ty = TREE_TYPE (t);
  tree sz = TYPE_SIZE_UNIT (ty);
  if (!sz || TREE_CODE (sz) != INTEGER_CST || TREE_CODE (off) != INTEGER_CST
      || !tree_int_cst_lt (off, sz))
    return false;
  *base = build_fold_addr_expr (t);
  *size = sz;
  return true;
}

static ex
decay (ex e, location_t loc)
{
  tree t = e.t;
  if (TREE_SIDE_EFFECTS (t) || !DECL_P (t))
    t = stabilize_reference (t);
  tree atype = TREE_TYPE (t);
  tree elt = TREE_TYPE (atype);
  if (e.nullbase)
    /* ((T *)0)->a decays (the kernel's container_of type check
       ((T *)0)->name[0] in typeof): an address with no allocation behind
       it; any access through it traps.  */
    return mk_parts (tzd_mptr_type (elt), array_first_addr (t, loc),
		     null_pointer_node, size_zero_node);
  if (e.tail)
    {
      /* Flexible-array idiom: up to the end of the enclosing allocation.  */
      tree addr = array_first_addr (t, loc);
      if (e.ob)
	return mk_parts (tzd_mptr_type (elt), addr, e.ob, e.os);
      tree root = get_base_address (t);
      if (root && DECL_P (root) && DECL_SIZE_UNIT (root))
	return mk_parts (tzd_mptr_type (elt), addr,
			 build_fold_addr_expr_loc (loc, root),
			 DECL_SIZE_UNIT (root));
      tree cb, cs;
      if (fam_container_bounds (t, &cb, &cs))
	return mk_parts (tzd_mptr_type (elt), addr, cb, cs);
    }
  tree size = TYPE_SIZE_UNIT (atype);
  tree root = get_base_address (t);
  bool host_var = root && VAR_P (root) && DECL_IN_SYSTEM_HEADER (root)
		  && DECL_NAME (root);
  const host_ptr_rule *arule = NULL;
  if (host_var)
    {
      /* A host array: its rule ("name[]") gives the element count when the
	 declaration has none, and the meaning of raw pointer elements.  */
      char nm[256];
      snprintf (nm, sizeof nm, "%s[]", IDENTIFIER_POINTER (DECL_NAME (root)));
      arule = host_ptr_rule_for (nm);
      if (arule && arule->ekind && raw_data_ptr_p (elt))
	elt = hrule_type (elt, arule->ekind, arule->ea, arule->eb);
    }
  if (size == NULL_TREE)
    {
      /* Incomplete array (extern T x[]): no default bounds (task #1).  */
      if (host_var && arule && arule->kind == 'o' && arule->a > 0
	  && TYPE_SIZE_UNIT (elt))
	size = size_binop (MULT_EXPR, TYPE_SIZE_UNIT (elt),
			   size_int (arule->a));
      else if (!host_var && root && VAR_P (root) && DECL_NAME (root)
	       && (TREE_PUBLIC (root)
		   || (TREE_STATIC (root) && DECL_FILE_SCOPE_P (root))))
	/* Defined in another Managed C unit, which publishes its size as
	   __tzd_size.NAME (emitted with every public array definition);
	   no such definition is a link error.  */
	size = build1 (NOP_EXPR, size_type_node, array_size_symbol (root));
      else
	{
	  error_at (loc, "size of array %qE is unknown here and has no "
		    "bounds rule (task #1)", root ? root : t);
	  return err ();
	}
    }
  tree addr = array_first_addr (t, loc);
  tree base = build_fold_addr_expr_loc (loc, t);
  return mk_parts (tzd_mptr_type (elt), addr, base, size);
}

static GTY(()) tree tzd_host_strsize_decl;
static GTY(()) tree tzd_host_vecsize_decl;
static GTY(()) tree tzd_host_wcssize_decl;
static GTY(()) tree tzd_host_mapsize_decl;
static GTY(()) tree tzd_host_shmsize_decl;

static void
init_host_rule_decls (void)
{
  if (tzd_host_strsize_decl)
    return;
  tzd_host_strsize_decl
    = libd_decl ("tzd_host_strsize",
		 build_function_type_list (size_type_node, const_ptr_type_node,
					   NULL_TREE));
  tzd_host_vecsize_decl
    = libd_decl ("tzd_host_vecsize",
		 build_function_type_list (size_type_node, const_ptr_type_node,
					   NULL_TREE));
  tzd_host_wcssize_decl
    = libd_decl ("tzd_host_wcssize",
		 build_function_type_list (size_type_node, const_ptr_type_node,
					   NULL_TREE));
  DECL_PURE_P (tzd_host_wcssize_decl) = 1;
  tzd_host_mapsize_decl
    = libd_decl ("tzd_host_mapsize",
		 build_function_type_list (size_type_node, const_ptr_type_node,
					   size_type_node, NULL_TREE));
  tzd_host_shmsize_decl
    = libd_decl ("tzd_host_shmsize",
		 build_function_type_list (size_type_node, const_ptr_type_node,
					   integer_type_node, NULL_TREE));
  tree ds[4] = { tzd_host_strsize_decl, tzd_host_vecsize_decl,
		 tzd_host_mapsize_decl, tzd_host_shmsize_decl };
  for (int i = 0; i < 4; i++)
    DECL_PURE_P (ds[i]) = 1;
}

/* Managed pointer for raw pointer value T of type RAW under rule
   (KIND, A, B), element rule (EKIND, EA, EB).  ARGB/ARGS are the bounds
   of the argument for 'g'; ARGN the integer argument for 'l'/'m'.  */
static ex
host_rule_apply (tree t, tree raw, char kind, int a, int b, char ekind,
		 int ea, int eb, tree argb, tree args, tree argn,
		 location_t loc)
{
  tree target = TREE_TYPE (raw);
  if (ekind)
    {
      if (!raw_data_ptr_p (target))
	{
	  error_at (loc, "internal: element rule on a non-pointer element");
	  return err ();
	}
      target = hrule_type (target, ekind, ea, eb);
    }
  init_host_rule_decls ();
  t = save_expr (fold_convert (ptr_type_node, t));
  tree nonnull = fold_build2_loc (loc, NE_EXPR, boolean_type_node, t,
				  null_pointer_node);
  tree base = t, size = size_zero_node;
  tree esz = TYPE_SIZE_UNIT (target);
  switch (kind)
    {
    case 's':
      size = build_call_expr_loc (loc, tzd_host_strsize_decl, 1, t);
      break;
    case 'v':
      size = build_call_expr_loc (loc, tzd_host_vecsize_decl, 1, t);
      break;
    case 'w':
      size = build_call_expr_loc (loc, tzd_host_wcssize_decl, 1, t);
      break;
    case 'x':
      break;
    case 'o':
    case 't':
      if (!esz || TREE_CODE (esz) != INTEGER_CST || VOID_TYPE_P (target))
	{
	  error_at (loc, "host pointer rule needs a complete pointee type");
	  return err ();
	}
      if (kind == 'o')
	size = fold_build2_loc (loc, MULT_EXPR, size_type_node, esz,
				size_int (a ? a : 1));
      else
	{
	  base = fold_build_pointer_plus_loc
		   (loc, t, fold_build1_loc (loc, NEGATE_EXPR, sizetype,
					     fold_build2_loc (loc, MULT_EXPR,
							      sizetype, esz,
							      size_int (a))));
	  size = fold_build2_loc (loc, MULT_EXPR, size_type_node, esz,
				  size_int (b));
	}
      break;
    case 'g':
    case 'c':
      {
	/* Inside the argument: its bounds.  Otherwise 'g' traps (the host
	   broke its contract) and 'c' is a host string (getcwd (NULL, 0)).  */
	tree ab = save_expr (fold_convert (ptr_type_node, argb));
	tree as = save_expr (fold_convert (size_type_node, args));
	tree off = fold_build2_loc (loc, MINUS_EXPR, size_type_node,
				    fold_convert (size_type_node, t),
				    fold_convert (size_type_node, ab));
	tree inside = fold_build2_loc
	  (loc, TRUTH_ANDIF_EXPR, boolean_type_node,
	   fold_build2_loc (loc, NE_EXPR, boolean_type_node, ab,
			    null_pointer_node),
	   fold_build2_loc (loc, LT_EXPR, boolean_type_node, off, as));
	inside = save_expr (inside);
	if (kind == 'g')
	  {
	    base = fold_build3_loc (loc, COND_EXPR, ptr_type_node, inside, ab,
				    build2 (COMPOUND_EXPR, ptr_type_node,
					    trap_call (loc, "host returned a "
						       "pointer outside its "
						       "argument"), ab));
	    size = as;
	  }
	else
	  {
	    base = fold_build3_loc (loc, COND_EXPR, ptr_type_node, inside, ab, t);
	    size = fold_build3_loc (loc, COND_EXPR, size_type_node, inside, as,
				    build_call_expr_loc (loc,
							 tzd_host_strsize_decl,
							 1, t));
	  }
      }
      break;
    case 'l':
      size = build_call_expr_loc (loc, tzd_host_mapsize_decl, 2, t,
				  fold_convert (size_type_node, argn));
      break;
    case 'm':
      size = build_call_expr_loc (loc, tzd_host_shmsize_decl, 2, t,
				  fold_convert (integer_type_node, argn));
      break;
    default:
      gcc_unreachable ();
    }
  /* NULL stays the null managed pointer.  */
  base = fold_build3_loc (loc, COND_EXPR, ptr_type_node, nonnull,
			  fold_convert (ptr_type_node, base), null_pointer_node);
  size = fold_build3_loc (loc, COND_EXPR, size_type_node, nonnull,
			  fold_convert (size_type_node, size), size_zero_node);
  return mk_parts (tzd_mptr_type (target), t, base, size);
}

static ex host_raw_to_managed (ex e, location_t loc);

/* Value of E (lvalue-to-rvalue, array and function decay).  */
static ex
rvalue (ex e, location_t loc)
{
  if (bad (e))
    return e;
  if (TREE_CODE (e.type) == ARRAY_TYPE && e.t)
    return decay (e, loc);
  if (TREE_CODE (e.type) == FUNCTION_TYPE)
    {
      tree fn = e.t;
      if (TREE_CODE (fn) == FUNCTION_DECL)
	{
	  TREE_USED (fn) = 1;
	  fn = build_fold_addr_expr_loc (loc, fn);
	}
      tree pt = build_pointer_type (e.type);
      if (TREE_TYPE (fn) != pt)
	fn = build1 (NOP_EXPR, pt, fn);   /* lowered real type (sv) */
      return mk (fn, pt);
    }
  if (e.nullbase && e.t)
    {
      e.t = build2 (COMPOUND_EXPR, TREE_TYPE (e.t),
		    trap_call (loc, "null pointer access"), e.t);
      e.nullbase = false;
    }
  if (e.t && e.lv && TZD_MSLOT_P (TREE_TYPE (e.t)))
    /* Managed pointer stored in memory: 8 bytes plus its shadow entry.  */
    return mk (slot_load (e.t, mrec (e.type), loc), e.type);
  if (e.t && raw_data_ptr_p (e.type))
    {
      if (native_region_p ())
	{
	  /* Inside system-header code (glibc inline function bodies): the
	     length of a raw pointer is unknown here and no rule can supply
	     one.  No unbounded pointer is accepted (TK, task #24).  */
	  error_at (loc, "host pointer of unknown length used inside a "
		    "system header function body; its length must be "
		    "declared by an annotation (task #24)");
	  tree t = save_expr (e.t);
	  return mk_parts (tzd_mptr_type (TREE_TYPE (e.type)), t, t,
			   TYPE_MAX_VALUE (ptrdiff_type_node));
	}
      /* A raw pointer from the host side: only through a bounds rule.  */
      return host_raw_to_managed (e, loc);
    }
  if (e.t && e.lv
      && (INTEGRAL_TYPE_P (e.type) || SCALAR_FLOAT_TYPE_P (e.type)
	  || TREE_CODE (e.type) == COMPLEX_TYPE
	  || TREE_CODE (e.type) == VECTOR_TYPE))
    /* A scalar read from memory reached through a pointer: heap bytes
       must have been written (task #8, CWE-908).  The value is read
       first, so the access's own checks (bounds, live block) come first.
       Aggregates are not tested here: copying a struct is not reading
       its padding (the copy carries the written bits instead).  */
    if (tree addr = mem_lvalue_addr (e.t, loc))
      if (tree chk = hp_access (addr, TYPE_SIZE_UNIT (e.type), true, loc))
	{
	  tree v = save_expr (e.t);
	  e.t = build2 (COMPOUND_EXPR, TREE_TYPE (v),
			build2 (COMPOUND_EXPR, void_type_node,
				fold_convert (void_type_node, v), chk), v);
	}
  e.lv = false;
  e.oa = e.ob = e.os = NULL_TREE;
  return e;
}

/* Name of the struct or union that declares FIELD, or NULL.  */
static const char *
field_owner_name (tree field)
{
  tree ctx = DECL_CONTEXT (field);
  if (!ctx || !TYPE_NAME (ctx))
    return NULL;
  tree n = TYPE_NAME (ctx);
  if (TREE_CODE (n) == TYPE_DECL)
    n = DECL_NAME (n);
  return n && TREE_CODE (n) == IDENTIFIER_NODE ? IDENTIFIER_POINTER (n) : NULL;
}

/* The rule for the raw pointer read from SRC (a host struct field, host
   variable, or element of a host array), described in DESC.  */
static const host_ptr_rule *
host_rule_lookup (tree src, char *desc, size_t n)
{
  while (CONVERT_EXPR_P (src) || TREE_CODE (src) == NON_LVALUE_EXPR)
    src = TREE_OPERAND (src, 0);
  if (TREE_CODE (src) == COMPONENT_REF)
    {
      tree f = TREE_OPERAND (src, 1);
      const char *owner = field_owner_name (f);
      snprintf (desc, n, "%s.%s", owner ? owner : "<anonymous>",
		DECL_NAME (f) ? IDENTIFIER_POINTER (DECL_NAME (f))
			      : "<anonymous>");
    }
  else if (VAR_P (src) && DECL_NAME (src))
    snprintf (desc, n, "%s", IDENTIFIER_POINTER (DECL_NAME (src)));
  else if (TREE_CODE (src) == CALL_EXPR)
    {
      /* A direct call the call path did not rule (GCC builtins such as
	 __builtin_frame_address): the rule of the callee's name.  */
      tree fn = get_callee_fndecl (src);
      if (fn && DECL_NAME (fn))
	{
	  snprintf (desc, n, "result of %s", IDENTIFIER_POINTER (DECL_NAME (fn)));
	  return host_ptr_rule_for (IDENTIFIER_POINTER (DECL_NAME (fn)));
	}
      snprintf (desc, n, "result of a call through a function pointer");
      return NULL;
    }
  else
    {
      snprintf (desc, n, "host expression");
      return NULL;
    }
  const host_ptr_rule *r = host_ptr_rule_for (desc);
  /* Rules that need call arguments only apply to calls.  */
  if (r && (r->kind == 'g' || r->kind == 'c' || r->kind == 'l'
	    || r->kind == 'm'))
    return NULL;
  return r;
}

/* Managed pointer for raw pointer value VAL of type RAW read from SRC, by
   its rule: an element rule carried by RAW, or the rule of SRC.  Without a
   rule: a compile error, or (RUNTIME) a trap when the value is used.  */
static ex
host_rule_value (tree val, tree raw, tree src, location_t loc, bool runtime)
{
  tree m = TYPE_MAIN_VARIANT (raw);
  if (TZD_HRULE_P (m))
    {
      tree v = TYPE_LANG_SLOT_1 (m);
      return host_rule_apply (val, raw,
			      (char) tree_to_shwi (TREE_VEC_ELT (v, 0)),
			      (int) tree_to_shwi (TREE_VEC_ELT (v, 1)),
			      (int) tree_to_shwi (TREE_VEC_ELT (v, 2)),
			      0, 0, 0, NULL_TREE, NULL_TREE, NULL_TREE, loc);
    }
  char desc[256];
  if (const host_ptr_rule *r = host_rule_lookup (src, desc, sizeof desc))
    return host_rule_apply (val, raw, r->kind, r->a, r->b, r->ekind, r->ea,
			    r->eb, NULL_TREE, NULL_TREE, NULL_TREE, loc);
  if (runtime)
    {
      char msg[320];
      snprintf (msg, sizeof msg, "pointer written by the host has no "
		"bounds rule (%s)", desc);
      tree t = build2 (COMPOUND_EXPR, TREE_TYPE (val), trap_call (loc, msg),
		       val);
      return mk_parts (tzd_mptr_type (TREE_TYPE (raw)), t, null_pointer_node,
		       size_zero_node);
    }
  error_at (loc, "no bounds rule for a pointer from the host (%s); "
	    "add one to host_ptr_rules (task #1)", desc);
  return err ();
}

/* Raw pointer rvalue E (from the host) as a managed pointer.  There is no
   default: a recorded entry, a rule, or an error / trap (task #1).  */
static ex
host_raw_to_managed (ex e, location_t loc)
{
  tree raw = e.type;
  tree s = e.t;
  while (CONVERT_EXPR_P (s) || TREE_CODE (s) == NON_LVALUE_EXPR)
    s = TREE_OPERAND (s, 0);
  if (TREE_CODE (s) == INTEGER_CST)
    /* NULL, MAP_FAILED: an integer as a pointer has no allocation (like
       an integer-to-pointer conversion in Managed C).  */
    return mk_parts (tzd_mptr_type (TREE_TYPE (raw)), e.t, null_pointer_node,
		     size_zero_node);
  if (VAR_P (s) && DECL_HARD_REGISTER (s))
    /* A pointer in a machine register: a raw address.  */
    return from_native (e.t, tzd_mptr_type (TREE_TYPE (raw)), loc);
  if (e.lv && !native_region_p ()
      && !(TREE_CODE (s) == COMPONENT_REF
	   && DECL_BIT_FIELD (TREE_OPERAND (s, 1))))
    {
      /* A raw pointer in memory.  If user code stored it, its bounds were
	 recorded at that address and still match the 8 bytes: use them.
	 Otherwise the host wrote it: its rule applies; without one the
	 load traps.  */
      init_shadow_decls ();
      mark_addressable_tree (e.t);
      tree addr = save_expr (build_fold_addr_expr_loc (loc, e.t));
      tree caddr = fold_convert (const_ptr_type_node, addr);
      tree val = build_fold_indirect_ref_loc (loc, addr);
      ex fb = host_rule_value (val, raw, e.t, loc, true);
      if (bad (fb))
	return fb;
      tree rec = mrec (fb.type);
      tree entry = build1 (VIEW_CONVERT_EXPR, rec,
			   build_call_expr_loc (loc, tzd_sh_load_decl, 1, caddr));
      tree has = build_call_expr_loc (loc, tzd_sh_match_decl, 1, caddr);
      tree r = build3 (COND_EXPR, rec,
		       fold_build2_loc (loc, NE_EXPR, boolean_type_node, has,
					integer_zero_node),
		       entry, whole (fb));
      return mk (r, fb.type);
    }
  return host_rule_value (e.t, raw, e.t, loc, false);
}

/* The GENERIC value of an rvalue.  */
static tree
value (const ex &e)
{
  if (bad (e))
    return error_mark_node;
  if (TZD_MPTR_P (e.type))
    return whole (e);
  return e.t;
}

static tree
value_void (const ex &e)
{
  if (bad (e))
    return error_mark_node;
  if (e.t == NULL_TREE)
    {
      tree r = NULL_TREE;
      tree p[3] = { e.pa, e.pb, e.ps };
      for (int i = 0; i < 3; i++)
	if (TREE_SIDE_EFFECTS (p[i]))
	  {
	    tree v = build1 (CONVERT_EXPR, void_type_node, p[i]);
	    r = r ? build2 (COMPOUND_EXPR, void_type_node, r, v) : v;
	  }
      return r ? r : build_empty_stmt (UNKNOWN_LOCATION);
    }
  return build1 (CONVERT_EXPR, void_type_node, e.t);
}

/* ---- the bounds check ---- */

static tree
trap_call (location_t loc, const char *what)
{
  if (tzd_trap_decl == NULL_TREE)
    {
      tree ft = build_function_type_list (void_type_node, const_ptr_type_node,
					  NULL_TREE);
      tzd_trap_decl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
				get_identifier ("tzd_trap"), ft);
      DECL_EXTERNAL (tzd_trap_decl) = 1;
      TREE_PUBLIC (tzd_trap_decl) = 1;
      TREE_THIS_VOLATILE (tzd_trap_decl) = 1;   /* noreturn */
      TREE_NOTHROW (tzd_trap_decl) = 1;
      DECL_ARTIFICIAL (tzd_trap_decl) = 1;
    }
  expanded_location xl = expand_location (loc);
  char *msg = xasprintf ("%s:%d: tzd: %s\n", xl.file ? xl.file : "?",
			 xl.line, what);
  tree s = build_string_literal (strlen (msg) + 1, msg);
  free (msg);
  TREE_USED (tzd_trap_decl) = 1;
  return build_call_expr_loc (loc, tzd_trap_decl, 1, s);
}

/* As trap_call, for a failed bounds check of N bytes at A against
   [B, B + S): the runtime's tzd_trap_bounds also prints a short dump of
   the access and the object (libc2d, kc2d).  */
static GTY(()) tree tzd_trap_bounds_decl;
/* kc2d's tzd_stack_view (`#pragma tzd stack_reader`, deref).  */
static GTY(()) tree stack_view_decl;
static tree
trap_bounds_call (location_t loc, const char *what, tree a, tree b, tree s,
		  tree n)
{
  if (tzd_trap_bounds_decl == NULL_TREE)
    {
      tree ft = build_function_type_list (void_type_node, const_ptr_type_node,
					  const_ptr_type_node,
					  const_ptr_type_node,
					  size_type_node, size_type_node,
					  NULL_TREE);
      tzd_trap_bounds_decl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
					 get_identifier ("tzd_trap_bounds"),
					 ft);
      DECL_EXTERNAL (tzd_trap_bounds_decl) = 1;
      TREE_PUBLIC (tzd_trap_bounds_decl) = 1;
      TREE_THIS_VOLATILE (tzd_trap_bounds_decl) = 1;   /* noreturn */
      TREE_NOTHROW (tzd_trap_bounds_decl) = 1;
      DECL_ARTIFICIAL (tzd_trap_bounds_decl) = 1;
    }
  expanded_location xl = expand_location (loc);
  char *msg = xasprintf ("%s:%d: tzd: %s\n", xl.file ? xl.file : "?",
			 xl.line, what);
  tree m = build_string_literal (strlen (msg) + 1, msg);
  free (msg);
  TREE_USED (tzd_trap_bounds_decl) = 1;
  return build_call_expr_loc (loc, tzd_trap_bounds_decl, 5, m,
			      fold_convert (const_ptr_type_node, a),
			      fold_convert (const_ptr_type_node, b),
			      fold_convert (size_type_node, s),
			      fold_convert (size_type_node, n));
}

/* Trap unless N bytes at A lie inside [B, B + S).  A must be stable.  */
/* T as a constant offset from a base object: &obj.f[2] + 8 -> (obj, n).  */
static bool
const_addr_split (tree t, tree *base, poly_int64 *off)
{
  poly_int64 acc = 0;
  for (;;)
    {
      while (CONVERT_EXPR_P (t) || TREE_CODE (t) == SAVE_EXPR)
	t = TREE_OPERAND (t, 0);
      if (TREE_CODE (t) == POINTER_PLUS_EXPR
	  && poly_int_tree_p (TREE_OPERAND (t, 1)))
	{
	  acc += tree_to_poly_int64 (TREE_OPERAND (t, 1));
	  t = TREE_OPERAND (t, 0);
	  continue;
	}
      break;
    }
  if (TREE_CODE (t) != ADDR_EXPR)
    return false;
  poly_int64 o;
  tree b = get_addr_base_and_unit_offset (TREE_OPERAND (t, 0), &o);
  if (!b)
    return false;
  *base = b;
  *off = acc + o;
  return true;
}

/* A - B in bytes when both are constant offsets from the same object.  */
static bool
const_addr_diff (tree a, tree b, poly_int64 *diff)
{
  tree ba, bb;
  poly_int64 oa, ob;
  if (!const_addr_split (a, &ba, &oa) || !const_addr_split (b, &bb, &ob)
      || !operand_equal_p (ba, bb, 0))
    return false;
  *diff = oa - ob;
  return true;
}

/* The bounds check with Intel MPX instructions (-ftzd-mpx, TK
   2026-11-17, experimental: what the check costs when the processor does
   the comparing).  The bounds still travel in the managed pointer; they
   are loaded into a bound register right at the access:

     bndmk  -1(B,S), %bnd0      bounds [B, B + S - 1]
     bndcl  A, %bnd0            #BR if A < B
     bndcu  L, %bnd0            #BR if L > B + S - 1   (L = A + N - 1)

   The three stay together: with BNDCFGU.BNDPRESERVE clear, any call,
   return or jump in between would reset the bound registers.

   An empty object (S == 0) has its upper bound below the lower one, so
   every address fails.  The null pointer (B == 0, S == 0) would wrap to
   "everything": its test stays in software, as in the other mode.
   A violation is the processor's #BR exception (SIGSEGV; libc2d's
   tzd_mpx_init installs a handler that reports it).  On a processor or
   an OS without MPX the three instructions do nothing at all, so every
   unit compiled this way makes the program run tzd_mpx_init first,
   which stops the program unless MPX is there and enabled
   (tzd_parse_file).  */
static tree trap_bounds_call (location_t, const char *, tree, tree, tree,
			      tree);

static tree
build_check_mpx (tree a, tree b, tree s, tree n, tree isnull, location_t loc)
{
  tree ul = long_unsigned_type_node;
  tree inputs = NULL_TREE;
  auto in = [&] (tree v)
    {
      tree c = build_tree_list (NULL_TREE, build_string (1, "r"));
      inputs = chainon (inputs, build_tree_list (c, fold_convert (ul, v)));
    };
  in (b);
  in (s);
  in (a);
  /* The last byte accessed, A + N - 1, in a register: bndcu also takes a
     memory operand (bndcu N-1(A), one instruction less when N is a
     constant), but QEMU 8.2's emulation of MPX decodes what follows
     that form as 32-bit code, and the emulation is the only place this
     mode can be tested without a Skylake..Comet Lake machine.  */
  tree last = fold_build2_loc (loc, MINUS_EXPR, ul,
			       fold_build2_loc (loc, PLUS_EXPR, ul,
						fold_convert (ul, a),
						fold_convert (ul, n)),
			       build_one_cst (ul));
  in (last);
  static const char text[]
    = "bndmk -1(%0,%1), %%bnd0; bndcl %2, %%bnd0; bndcu %3, %%bnd0";
  tree chk = build5_loc (loc, ASM_EXPR, void_type_node,
			 build_string (sizeof text - 1, text),
			 NULL_TREE, inputs, NULL_TREE, NULL_TREE);
  ASM_VOLATILE_P (chk) = 1;
  /* A, S and N are used on both arms: evaluate them before the test (a
     SAVE_EXPR first reached inside one arm would be unset on the other;
     the software check has them all in its condition).  */
  tree cond = isnull;
  for (tree v : { n, s, a })
    if (TREE_SIDE_EFFECTS (v))
      cond = build2_loc (loc, COMPOUND_EXPR, TREE_TYPE (isnull),
			 fold_convert (void_type_node, v), cond);
  return build3 (COND_EXPR, void_type_node, cond,
		 trap_bounds_call (loc, "null pointer access", a, b, s, n),
		 chk);
}

/* The heap map (task #8, TK 2026-11-28; libc2d stdlib.c).  The heap is one
   region [tzd_hp_lo, tzd_hp_lo + tzd_hp_span), and tzd_hp_map has 2 bits
   per 16-byte granule of it: bit 0, the granule belongs to a live block;
   bit 1, it was written.  An access through a pointer whose base lies in
   the region tests bit 0 (use after free, CWE-416: build_check); a scalar
   read tests bit 1 (a read of heap bytes never written, CWE-908: rvalue);
   a store sets it (through tzd_hp_wrote, only when it is not set yet).
   Outside the heap (stack, static, host memory) the range test fails and
   nothing else is done.  Not in the kernel: kc2d has its own allocator.  */
static GTY(()) tree hp_lo_decl;
static GTY(()) tree hp_span_decl;
static GTY(()) tree hp_map_decl;
static GTY(()) tree hp_wrote_decl;
static GTY(()) tree hp_read_decl;
static GTY(()) tree hp_handout_decl;
static GTY(()) tree copy_bytes_decl;

static bool
heap_map_p (void)
{
  return !flag_tzd_kernel;
}

static void
init_hp_decls (void)
{
  if (hp_lo_decl)
    return;
  auto var = [] (const char *name, tree type)
    {
      tree d = build_decl (BUILTINS_LOCATION, VAR_DECL, get_identifier (name),
			   type);
      TREE_PUBLIC (d) = 1;
      DECL_EXTERNAL (d) = 1;
      DECL_ARTIFICIAL (d) = 1;
      return d;
    };
  hp_lo_decl = var ("tzd_hp_lo", long_unsigned_type_node);
  hp_span_decl = var ("tzd_hp_span", long_unsigned_type_node);
  hp_map_decl = var ("tzd_hp_map", build_pointer_type (unsigned_char_type_node));
  tree rn = build_function_type_list (void_type_node, const_ptr_type_node,
				      size_type_node, NULL_TREE);
  hp_wrote_decl = libd_decl ("tzd_hp_wrote", rn);
  hp_read_decl = libd_decl ("tzd_hp_read", rn);
  hp_handout_decl = libd_decl ("tzd_hp_handout",
			       build_function_type_list (ptr_type_node,
							 ptr_type_node,
							 const_ptr_type_node,
							 size_type_node,
							 NULL_TREE));
  copy_bytes_decl = libd_decl ("tzd_copy_bytes",
			       build_function_type_list (void_type_node,
							 ptr_type_node,
							 const_ptr_type_node,
							 size_type_node,
							 NULL_TREE));
}

/* (size_t) ADDR - tzd_hp_lo: the offset of ADDR in the heap region.  */
static tree
hp_off (tree addr)
{
  init_hp_decls ();
  return save_expr (fold_build2 (MINUS_EXPR, size_type_node,
				 fold_convert (size_type_node, addr),
				 fold_convert (size_type_node, hp_lo_decl)));
}

/* Whether offset OFF lies in the heap region.  */
static tree
hp_in (tree off)
{
  return fold_build2 (LT_EXPR, integer_type_node, off,
		      fold_convert (size_type_node, hp_span_decl));
}

/* Bit K (0: live, 1: written) of the granule at offset OFF, as 0 or 1.
   Read only when hp_in (OFF) holds.  */
static tree
hp_bit (tree off, int k)
{
  tree idx = fold_build2 (RSHIFT_EXPR, size_type_node, off, size_int (6));
  tree byte = build1 (INDIRECT_REF, unsigned_char_type_node,
		      fold_build_pointer_plus (hp_map_decl, idx));
  tree sh = fold_build2 (BIT_AND_EXPR, size_type_node,
			 fold_build2 (RSHIFT_EXPR, size_type_node, off,
				      size_int (3)),
			 size_int (6));
  if (k)
    sh = fold_build2 (PLUS_EXPR, size_type_node, sh, size_int (1));
  tree v = fold_build2 (RSHIFT_EXPR, unsigned_type_node,
			fold_convert (unsigned_type_node, byte),
			fold_convert (unsigned_type_node, sh));
  return fold_build2 (BIT_AND_EXPR, unsigned_type_node, v,
		      build_one_cst (unsigned_type_node));
}

/* The N bytes at native address ADDR are read (LOAD) or written: a call of
   tzd_hp_read / tzd_hp_wrote when ADDR is heap memory and its granule's
   written bit is clear, or the access runs into the next granule.  The
   common case -- not heap memory, or a written granule -- costs a compare
   and a load.  NULL_TREE when there is nothing to do here.  */
static tree
hp_access (tree addr, tree n, bool load, location_t loc)
{
  if (!heap_map_p () || !current_function_decl || static_init_depth
      || unevaluated || !n || TREE_CODE (n) != INTEGER_CST || integer_zerop (n))
    return NULL_TREE;
  addr = save_expr (addr);
  tree off = hp_off (addr);
  tree cross = fold_build2 (GT_EXPR, integer_type_node,
			    fold_build2 (PLUS_EXPR, size_type_node,
					 fold_build2 (BIT_AND_EXPR,
						      size_type_node, off,
						      size_int (15)),
					 fold_convert (size_type_node, n)),
			    size_int (16));
  tree need = fold_build2 (TRUTH_ANDIF_EXPR, integer_type_node, hp_in (off),
			   fold_build2 (TRUTH_ORIF_EXPR, integer_type_node,
					fold_build2 (EQ_EXPR, integer_type_node,
						     hp_bit (off, 1),
						     build_zero_cst
						       (unsigned_type_node)),
					cross));
  tree call = build_call_expr_loc (loc, load ? hp_read_decl : hp_wrote_decl, 2,
				   fold_convert (const_ptr_type_node, addr),
				   fold_convert (size_type_node, n));
  return build3 (COND_EXPR, void_type_node, need, call,
		 build_empty_stmt (loc));
}

/* The native address of memory lvalue L when it is reached through a
   pointer (an INDIRECT_REF at its base, under member and element
   references), rebuilt on the dereferenced pointer value itself: the
   bounds check that deref () put into the address is left out (it runs
   with the access).  NULL_TREE for a declared object (it is never heap
   memory), a bit-field, or anything that would be evaluated twice.  */
static tree
mem_lvalue_addr (tree l, location_t loc)
{
  auto_vec<tree, 8> chain;
  tree t = l;
  while (TREE_CODE (t) == COMPONENT_REF || TREE_CODE (t) == ARRAY_REF)
    {
      if (TREE_CODE (t) == COMPONENT_REF
	  && DECL_BIT_FIELD (TREE_OPERAND (t, 1)))
	return NULL_TREE;
      if (TREE_CODE (t) == ARRAY_REF
	  && TREE_SIDE_EFFECTS (TREE_OPERAND (t, 1)))
	return NULL_TREE;
      chain.safe_push (t);
      t = TREE_OPERAND (t, 0);
    }
  if (TREE_CODE (t) != INDIRECT_REF)
    return NULL_TREE;
  tree p = TREE_OPERAND (t, 0);
  tree q = p;
  for (;;)
    {
      STRIP_NOPS (q);
      if (TREE_CODE (q) != COMPOUND_EXPR)
	break;
      q = TREE_OPERAND (q, 1);
    }
  if (TREE_CODE (q) == ADDR_EXPR)
    return NULL_TREE;
  if (TREE_SIDE_EFFECTS (q) && TREE_CODE (q) != SAVE_EXPR)
    return NULL_TREE;
  tree cur = copy_node (t);
  TREE_OPERAND (cur, 0) = fold_convert (TREE_TYPE (p), q);
  for (unsigned i = chain.length (); i-- > 0;)
    {
      tree c = copy_node (chain[i]);
      TREE_OPERAND (c, 0) = cur;
      cur = c;
    }
  return build_fold_addr_expr_loc (loc, cur);
}

/* The mark of a store of TYPE into lvalue L, or NULL_TREE.  */
static tree
hp_store_mark (tree l, tree type, location_t loc)
{
  if (!heap_map_p () || !current_function_decl || static_init_depth
      || !TYPE_SIZE_UNIT (type) || TREE_CODE (TYPE_SIZE_UNIT (type)) != INTEGER_CST)
    return NULL_TREE;
  tree addr = mem_lvalue_addr (l, loc);
  return addr ? hp_access (addr, TYPE_SIZE_UNIT (type), false, loc) : NULL_TREE;
}

static ex convert_to (ex e, tree type, location_t loc, bool explicit_p = false);

/* Managed pointer A passed to a host function as raw pointer type RAW (a
   call argument, task #8): the host may write the bytes without being
   seen, so the rest of a heap object from the address on counts as
   written (tzd_hp_handout).  The record is evaluated before the test: a
   part of it first computed inside one arm would be unset on the other.
   Anything else goes through convert_to unchanged.  */
static ex
handout_arg (ex a, tree raw, location_t loc)
{
  a = rvalue (a, loc);
  if (bad (a) || !TZD_MPTR_P (a.type) || null_mptr_p (a) || fnptr_p (raw)
      || !heap_map_p () || !current_function_decl || static_init_depth
      || unevaluated)
    return convert_to (a, raw, loc, false);
  init_hp_decls ();
  tree v = save_expr (value (a));
  ex ev = mk (v, a.type);
  parts (ev);
  tree pa = fold_convert (ptr_type_node, ev.pa);
  tree call = build_call_expr_loc (loc, hp_handout_decl, 3, pa,
				   fold_convert (const_ptr_type_node, ev.pb),
				   fold_convert (size_type_node, ev.ps));
  tree r = build3 (COND_EXPR, ptr_type_node, hp_in (hp_off (ev.pb)), call, pa);
  r = build2 (COMPOUND_EXPR, ptr_type_node, fold_convert (void_type_node, v), r);
  return mk (fold_convert (raw, r), raw);
}

static tree
build_check (tree a, tree b, tree s, tree n, location_t loc)
{
  /* Nothing is read or written: an empty struct (the kernel's UP
     spinlock_t) behind ZERO_SIZE_PTR, as GCC's C does no access either.  */
  if (n && TREE_CODE (n) == INTEGER_CST && integer_zerop (n))
    return NULL_TREE;
  /* Statically in bounds: the base is the address of a declared object
     (never null) and A is a known constant distance into it
     (&nd[0].head in a static initializer must stay a constant).  */
  {
    tree bb = b, aa = a;
    STRIP_NOPS (bb);
    while (TREE_CODE (aa) == SAVE_EXPR || CONVERT_EXPR_P (aa))
      aa = TREE_OPERAND (aa, 0);
    poly_int64 diff;
    if (TREE_CODE (bb) == ADDR_EXPR && DECL_P (TREE_OPERAND (bb, 0))
	&& !DECL_WEAK (TREE_OPERAND (bb, 0))
	&& TREE_CODE (s) == INTEGER_CST && TREE_CODE (n) == INTEGER_CST
	&& const_addr_diff (aa, bb, &diff) && diff.is_constant ()
	&& known_ge (diff, 0) && tree_fits_uhwi_p (s) && tree_fits_uhwi_p (n)
	&& (unsigned HOST_WIDE_INT) diff.to_constant () + tree_to_uhwi (n)
	   <= tree_to_uhwi (s))
      return NULL_TREE;
  }
  tree ua = fold_convert (size_type_node, a);
  tree ub = fold_convert (size_type_node, b);
  n = fold_convert (size_type_node, n);
  tree off = fold_build2_loc (loc, MINUS_EXPR, size_type_node, ua, ub);
  tree isnull = fold_build2_loc (loc, EQ_EXPR, integer_type_node, b,
				 null_pointer_node);
  /* Out of bounds: OFF > S or S - OFF < N, written as
       S < N  ||  OFF > S - N
     (the same set: with S >= N, OFF > S - N covers both; with S < N no
     OFF is in bounds).  In this form only the second test depends on the
     address: in a loop over one object the first, like the null test, is
     loop-invariant and is hoisted, leaving one compare per access (the
     old form, OFF > S || S - OFF < N, kept two; task #69).  */
  s = fold_convert (size_type_node, s);
  tree oob = fold_build2_loc (loc, TRUTH_OR_EXPR, integer_type_node,
			      fold_build2_loc (loc, LT_EXPR,
					       integer_type_node, s, n),
			      fold_build2_loc (loc, GT_EXPR,
					       integer_type_node, off,
					       fold_build2_loc (loc,
								MINUS_EXPR,
								size_type_node,
								s, n)));
  tree badc = fold_build2_loc (loc, TRUTH_OR_EXPR, integer_type_node, isnull,
			       oob);
  if (integer_zerop (badc))
    return NULL_TREE;
  /* In bounds, but is the object still there?  A base in the heap region
     must lie in a live block (task #8, CWE-416).  The base, not the
     address: it is inside the object for every pointer that passed the
     bounds test, and it does not change along a loop over the object, so
     the test is hoisted with the null test.  Bounds first: an address
     outside them gets the out-of-bounds message.  */
  tree dead = NULL_TREE;
  if (heap_map_p ())
    {
      tree off = hp_off (b);
      dead = fold_build2 (TRUTH_ANDIF_EXPR, integer_type_node, hp_in (off),
			  fold_build2 (EQ_EXPR, integer_type_node,
				       hp_bit (off, 0),
				       build_zero_cst (unsigned_type_node)));
      if (TREE_CODE (n) != INTEGER_CST)
	/* Nothing is read or written by an empty access.  */
	dead = fold_build2 (TRUTH_ANDIF_EXPR, integer_type_node,
			    fold_build2 (NE_EXPR, integer_type_node, n,
					 build_zero_cst (size_type_node)),
			    dead);
    }
  tree freed = dead ? trap_bounds_call (loc, "use of freed heap memory",
					a, b, s, n) : NULL_TREE;
  if (flag_tzd_mpx)
    {
      tree chk = build_check_mpx (a, b, s, n, isnull, loc);
      if (dead)
	chk = build2 (COMPOUND_EXPR, void_type_node, chk,
		      build3 (COND_EXPR, void_type_node, dead, freed,
			      build_empty_stmt (loc)));
      return chk;
    }
  tree trap = build3 (COND_EXPR, void_type_node, isnull,
		      trap_bounds_call (loc, "null pointer access", a, b, s, n),
		      trap_bounds_call (loc, "out-of-bounds pointer access",
					a, b, s, n));
  tree chk = build3 (COND_EXPR, void_type_node, badc, trap,
		     build_empty_stmt (loc));
  if (dead)
    chk = build2 (COMPOUND_EXPR, void_type_node, chk,
		  build3 (COND_EXPR, void_type_node, dead, freed,
			  build_empty_stmt (loc)));
  return chk;
}

/* The member ID of struct/union ST, also inside anonymous members, with
   its byte offset in *OFF; NULL for a bit-field, a variable-size or
   variable-place member, or none.  */
static tree
member_path (tree st, tree id, tree *off)
{
  for (tree f = TYPE_FIELDS (st); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      if (TREE_CODE (byte_position (f)) != INTEGER_CST)
	continue;
      if (DECL_NAME (f) == id)
	{
	  if (DECL_BIT_FIELD (f) || !DECL_SIZE_UNIT (f)
	      || TREE_CODE (DECL_SIZE_UNIT (f)) != INTEGER_CST)
	    return NULL_TREE;
	  *off = byte_position (f);
	  return f;
	}
      if (!DECL_NAME (f) && RECORD_OR_UNION_TYPE_P (TREE_TYPE (f)))
	{
	  tree inner;
	  if (tree g = member_path (TREE_TYPE (f), id, &inner))
	    {
	      *off = size_binop (PLUS_EXPR, byte_position (f), inner);
	      return g;
	    }
	}
    }
  return NULL_TREE;
}

/* *P as an lvalue.  FIELD (P->FIELD, a named non-bit-field member of
   *P): only the bytes of that member are checked, not the whole *P (the
   kernel's list_for_each_entry_safe reads pos->member.next of a pos made
   from the list head, which is in bounds as a list_head only).  */
static ex
deref (ex p, location_t loc, tree field = NULL_TREE,
       tree foff = NULL_TREE)
{
  p = rvalue (p, loc);
  if (bad (p))
    return p;
  if (fnptr_p (p.type))
    /* (*fp) is the function itself.  */
    return mk (p.t, TREE_TYPE (p.type));
  if (!TZD_MPTR_P (p.type))
    {
      error_at (loc, "invalid type argument of unary %<*%>");
      return err ();
    }
  tree target = TZD_MPTR_TARGET (p.type);
  if (VOID_TYPE_P (target) && unevaluated)
    /* sizeof (*(void *) p) and the like: never evaluated (GNU C).  */
    return mk (build1 (NOP_EXPR, void_type_node, integer_zero_node),
	       void_type_node);
  if (VOID_TYPE_P (target) || !COMPLETE_TYPE_P (target))
    {
      error_at (loc, "dereferencing pointer to incomplete type");
      return err ();
    }
  if (null_mptr_p (p))
    {
      /* ((T *)0)->f: keep the address computable (offsetof idiom).  */
      ex r = mk (build1 (INDIRECT_REF, target,
			 build_int_cst (build_pointer_type (target), 0)),
		 target, true);
      r.nullbase = true;
      return r;
    }
  parts (p);
  tree a = save_expr (p.pa);
  tree chk;
  /* `#pragma tzd stack_reader` names this function (task #72, the
     kernel's stack unwinder): a word read through P goes through kc2d's
     tzd_stack_view (address, base, size).  In bounds, it is the address
     itself; out of bounds but inside a kernel stack of the running task,
     the address of a private copy of the word (read-only in effect: a
     store through it never reaches the stack); anywhere else, the trap.  */
  bool view = !field && flag_tzd_kernel && current_function_decl
	      && DECL_NAME (current_function_decl)
	      && tzd_stack_reader_p (IDENTIFIER_POINTER
				       (DECL_NAME (current_function_decl)))
	      && (INTEGRAL_TYPE_P (target) || POINTER_TYPE_P (target))
	      && TYPE_SIZE_UNIT (target)
	      && tree_fits_uhwi_p (TYPE_SIZE_UNIT (target))
	      && tree_to_uhwi (TYPE_SIZE_UNIT (target)) == 8;
  if (view)
    chk = NULL_TREE;
  else
  if (field && last_field_p (field) && fam_array_type_p (TREE_TYPE (field)))
    /* p->arr for a trailing flexible array: its address (it decays, no
       read), bounded by *p's own bounds, so an element read still checks
       against them (container_of(NULL, struct tnode, rcu)->kv at the end
       of fib_trie's tnode_free loop).  A narrowed array member keeps its
       check: its bounds would come from the address alone.  */
    chk = NULL_TREE;
  else if (field)
    chk = build_check (fold_build_pointer_plus_loc (loc, a,
						    foff ? foff
						    : byte_position (field)),
		       p.pb, p.ps, DECL_SIZE_UNIT (field), loc);
  else
    chk = build_check (a, p.pb, p.ps, TYPE_SIZE_UNIT (target), loc);
  tree addr = chk ? build2 (COMPOUND_EXPR, TREE_TYPE (a), chk, a) : a;
  if (view)
    {
      if (!stack_view_decl)
	{
	  stack_view_decl
	    = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
			  get_identifier ("tzd_stack_view"),
			  build_function_type_list (ptr_type_node,
						    const_ptr_type_node,
						    const_ptr_type_node,
						    size_type_node, NULL_TREE));
	  DECL_EXTERNAL (stack_view_decl) = 1;
	  TREE_PUBLIC (stack_view_decl) = 1;
	  TREE_NOTHROW (stack_view_decl) = 1;
	  DECL_ARTIFICIAL (stack_view_decl) = 1;
	}
      TREE_USED (stack_view_decl) = 1;
      addr = fold_convert (TREE_TYPE (a),
			   build_call_expr_loc (loc, stack_view_decl, 3,
						fold_convert (const_ptr_type_node, a),
						fold_convert (const_ptr_type_node, p.pb),
						fold_convert (size_type_node, p.ps)));
      chk = addr;
    }
  tree ref = build1 (INDIRECT_REF, target, addr);
  if (!chk && TREE_CODE (addr) == ADDR_EXPR)
    {
      /* *&x is x: an address constant built on it (&(&key)->f in a
	 static key's initializer) must not keep the INDIRECT_REF, which
	 GCC's constant pool cannot hash.  */
      tree f = fold_indirect_ref_loc (loc, ref);
      if (f != ref && TYPE_MAIN_VARIANT (TREE_TYPE (f))
		      == TYPE_MAIN_VARIANT (target))
	ref = f;
    }
  else if (!chk && static_init_depth == 0)
    {
      /* *(&x + C) (a constant address the check proved in bounds, as
	 &apic_maps[1].map[0]): MEM_REF[&x, C], which GCC's constant pool
	 can hash; an INDIRECT_REF of it cannot.  Not in a static
	 initializer: there the address must pass initializer_constant_valid_p
	 (staticp does not look into a MEM_REF) and is emitted directly.  */
      tree p = addr;
      STRIP_NOPS (p);
      tree base;
      poly_int64 off;
      /* GCC expects the base of such a MEM_REF to be a declaration
	 (tree_could_trap_p reads its DECL_SIZE_UNIT).  */
      if (TREE_CODE (p) == POINTER_PLUS_EXPR
	  && const_addr_split (p, &base, &off) && DECL_P (base)
	  && off.is_constant ())
	ref = build2_loc (loc, MEM_REF, target, build_fold_addr_expr (base),
			  build_int_cst (build_pointer_type (target),
					 off.to_constant ()));
    }
  if (TYPE_VOLATILE (target))
    TREE_THIS_VOLATILE (ref) = 1;
  if (TREE_SIDE_EFFECTS (addr))
    TREE_SIDE_EFFECTS (ref) = 1;
  ex r = mk (ref, target, true);
  r.oa = a;
  r.ob = p.pb;
  r.os = p.ps;
  return r;
}

/* &E.  */
/* &E computes an address and reads nothing, so the bounds check of the
   dereference E is built on (*p in &p->f, &(*p).a.b, &*p) is dropped:
   TzD traps when memory is used, not when an address is formed (the
   kernel's list_for_each_entry forms &pos->member of a pos made by
   container_of from the list head, which is not an entry).  Checks of
   pointers loaded on the way (p->q in &p->q->f) stay: they are reads.  */
static tree
drop_base_check (tree t)
{
  tree *pt = &t;
  while (TREE_CODE (*pt) == COMPONENT_REF || TREE_CODE (*pt) == ARRAY_REF)
    pt = &TREE_OPERAND (*pt, 0);
  if (TREE_CODE (*pt) != INDIRECT_REF)
    return t;
  tree op = TREE_OPERAND (*pt, 0);
  if (!(TREE_CODE (op) == COMPOUND_EXPR
	&& TREE_CODE (TREE_OPERAND (op, 0)) == COND_EXPR
	&& VOID_TYPE_P (TREE_TYPE (TREE_OPERAND (op, 0)))))
    return t;
  tree a = TREE_OPERAND (op, 1);
  tree type = TREE_TYPE (*pt);
  /* As deref does for an unchecked address: *&x is x, and *(&x + C) is
     MEM_REF[&x, C] (GCC's constant pool cannot hash an INDIRECT_REF in
     an address constant such as &per_cpu(cyc2ns.data[1].x)).  */
  tree p = a;
  STRIP_NOPS (p);
  tree base;
  poly_int64 off;
  if (TREE_CODE (p) == ADDR_EXPR)
    {
      tree f = fold_indirect_ref_loc (EXPR_LOCATION (*pt),
				      build1 (INDIRECT_REF, type, a));
      if (TREE_CODE (f) != INDIRECT_REF
	  && TYPE_MAIN_VARIANT (TREE_TYPE (f)) == TYPE_MAIN_VARIANT (type))
	{
	  *pt = f;
	  return t;
	}
    }
  else if (static_init_depth == 0 && TREE_CODE (p) == POINTER_PLUS_EXPR
	   && const_addr_split (p, &base, &off) && DECL_P (base)
	   && off.is_constant ())
    {
      *pt = build2 (MEM_REF, type, build_fold_addr_expr (base),
		    build_int_cst (build_pointer_type (type),
				   off.to_constant ()));
      return t;
    }
  TREE_OPERAND (*pt, 0) = a;
  return t;
}

static ex
address_of (ex e, location_t loc)
{
  if (bad (e))
    return e;
  if (TREE_CODE (e.type) == FUNCTION_TYPE)
    return rvalue (e, loc);
  if (!e.lv)
    {
      error_at (loc, "lvalue required as unary %<&%> operand");
      return err ();
    }
  tree ptype = tzd_mptr_type (e.type);
  if (TZD_MREC_P (TREE_TYPE (e.t)))
    {
      /* A pointer variable kept as a value record (its address is never
	 taken in the function; see addr_taken).  Cannot happen unless the
	 pre-scan missed a use.  */
      error_at (loc, "tzd: internal: address of a register pointer variable");
      return err ();
    }
  if (e.nullbase)
    /* &((T *)0)->f: a constant address with no allocation behind it.  */
    return mk_parts (ptype, fold_offsetof (e.t, build_pointer_type (e.type)),
		     null_pointer_node, size_zero_node);
  if (e.oa)
    return mk_parts (ptype, e.oa, e.ob, e.os);
  if (TREE_CODE (e.type) == ARRAY_TYPE && !COMPLETE_TYPE_P (e.type))
    {
      /* &x for extern T x[]: the bounds of x itself (its size comes from
	 the defining unit, as when x decays).  */
      ex d = decay (e, loc);
      if (bad (d))
	return d;
      parts (d);
      return mk_parts (ptype, d.pa, d.pb, d.ps);
    }
  if (VOID_TYPE_P (e.type))
    {
      /* extern void sym (a linker symbol, GNU C): an address with no bytes
	 behind it; reading through it traps.  */
      mark_addressable (e.t);
      tree addr = save_expr (build_fold_addr_expr_with_type_loc
			       (loc, e.t, build_pointer_type (e.type)));
      return mk_parts (ptype, addr, addr, size_zero_node);
    }
  mark_addressable (e.t);
  e.t = drop_base_check (e.t);
  tree addr = build_fold_addr_expr_with_type_loc (loc, e.t,
						  build_pointer_type (e.type));
  if (e.sub)
    {
      /* &s.f / &p->f: bounds of the member only, so writes through it
	 cannot reach neighbouring members (task #21).  */
      addr = save_expr (addr);
      return mk_parts (ptype, addr, addr,
		       e.subsize ? e.subsize : TYPE_SIZE_UNIT (e.type));
    }
  if (e.ob)
    return mk_parts (ptype, addr, e.ob, e.os);
  tree root = get_base_address (e.t);
  if (root && root == e.t && VAR_P (root) && DECL_EXTERNAL (root)
      && TREE_PUBLIC (root) && DECL_NAME (root)
      && !DECL_IN_SYSTEM_HEADER (root) && ends_in_fam_p (TREE_TYPE (root)))
    /* &x for extern struct S x whose S ends in a flexible array: the
       defining unit may initialize elements of it (the kernel's
       init_struct_pid.numbers), so x's size comes from there
       (__tzd_size.x), not from the type.  */
    return mk_parts (ptype, addr, addr,
		     build1 (NOP_EXPR, size_type_node,
			     array_size_symbol (root)));
  if (root && DECL_P (root) && DECL_SIZE_UNIT (root))
    return mk_parts (ptype, addr, build_fold_addr_expr_loc (loc, root),
		     DECL_SIZE_UNIT (root));
  if (root && TREE_CODE (root) == STRING_CST)
    return mk_parts (ptype, addr, build_fold_addr_expr_loc (loc, root),
		     TYPE_SIZE_UNIT (TREE_TYPE (root)));
  if (!COMPLETE_TYPE_P (e.type))
    {
      /* &x for extern struct S x with S incomplete here: x's size comes
	 from the unit that defines x (__tzd_size.x, as for extern arrays;
	 missing = link error).  */
      if (root && VAR_P (root) && DECL_NAME (root) && root == e.t
	  && TREE_PUBLIC (root))
	return mk_parts (ptype, addr, addr,
			 build1 (NOP_EXPR, size_type_node,
				 array_size_symbol (root)));
      error_at (loc, "address of an object of incomplete type");
      return err ();
    }
  return mk_parts (ptype, addr, addr, TYPE_SIZE_UNIT (e.type));
}

/* P + N (N integral).  */
/* Element size for pointer arithmetic: void counts as 1 byte (GNU C, as
   the kernel's (void *)p + n).  */
static tree
arith_size (tree target)
{
  return VOID_TYPE_P (target) ? size_one_node : TYPE_SIZE_UNIT (target);
}

static ex
ptr_add (ex p, tree n, bool negate, location_t loc)
{
  tree target = TZD_MPTR_TARGET (p.type);
  if (!VOID_TYPE_P (target) && !COMPLETE_TYPE_P (target))
    {
      error_at (loc, "pointer arithmetic on pointer to incomplete type");
      return err ();
    }
  parts (p);
  tree off = fold_convert (ssizetype, n);
  if (negate)
    off = fold_build1_loc (loc, NEGATE_EXPR, ssizetype, off);
  off = fold_build2_loc (loc, MULT_EXPR, sizetype,
			 fold_convert (sizetype, off),
			 arith_size (target));
  tree a = fold_build_pointer_plus_loc (loc, p.pa, off);
  return mk_parts (p.type, a, p.pb, p.ps);
}

static ex
ptr_diff (ex p, ex q, location_t loc)
{
  tree target = TZD_MPTR_TARGET (p.type);
  if (TYPE_MAIN_VARIANT (target)
      != TYPE_MAIN_VARIANT (TZD_MPTR_TARGET (q.type)))
    {
      error_at (loc, "invalid operands to binary - (pointers to different "
		"types)");
      return err ();
    }
  if (!VOID_TYPE_P (target) && !COMPLETE_TYPE_P (target))
    {
      error_at (loc, "pointer arithmetic on pointer to incomplete type");
      return err ();
    }
  parts (p);
  parts (q);
  tree qa = fold_convert (TREE_TYPE (p.pa), q.pa);
  tree d = fold_build2_loc (loc, POINTER_DIFF_EXPR, ptrdiff_type_node, p.pa,
			    qa);
  d = fold_build2_loc (loc, TRUNC_DIV_EXPR, ptrdiff_type_node, d,
		       fold_convert (ptrdiff_type_node,
				     arith_size (target)));
  /* Provisional (task #21): no trap across allocations.  The difference is
     only an integer; any pointer rebuilt from it is still checked against
     its own allocation.  Member pointers (&s.f) have their own base now,
     so &s.f - &s must work.  */
  return mk (d, ptrdiff_type_node);
}

/* ---- conversions ---- */

static bool
arith_p (tree t)
{
  return INTEGRAL_TYPE_P (t) || SCALAR_FLOAT_TYPE_P (t);
}

static tree
promote_type (tree t)
{
  t = TYPE_MAIN_VARIANT (t);
  if (INTEGRAL_TYPE_P (t)
      && TYPE_PRECISION (t) < TYPE_PRECISION (integer_type_node))
    return integer_type_node;
  if (TREE_CODE (t) == BOOLEAN_TYPE || TREE_CODE (t) == ENUMERAL_TYPE)
    return integer_type_node;
  return t;
}

/* Usual arithmetic conversions.  */
static tree
common_arith (tree a, tree b)
{
  a = promote_type (a);
  b = promote_type (b);
  if (SCALAR_FLOAT_TYPE_P (a) || SCALAR_FLOAT_TYPE_P (b))
    {
      if (!SCALAR_FLOAT_TYPE_P (b))
	return a;
      if (!SCALAR_FLOAT_TYPE_P (a))
	return b;
      return TYPE_PRECISION (a) >= TYPE_PRECISION (b) ? a : b;
    }
  if (a == b)
    return a;
  if (TYPE_PRECISION (a) != TYPE_PRECISION (b))
    return TYPE_PRECISION (a) > TYPE_PRECISION (b) ? a : b;
  if (TYPE_UNSIGNED (a))
    return a;
  if (TYPE_UNSIGNED (b))
    return b;
  return a == long_long_integer_type_node ? a : b;
}

static void
pointer_mismatch (tree to, tree from, location_t loc)
{
  tree a = TYPE_MAIN_VARIANT (TZD_MPTR_TARGET (to));
  tree b = TYPE_MAIN_VARIANT (TZD_MPTR_TARGET (from));
  if (a != b && !VOID_TYPE_P (a) && !VOID_TYPE_P (b))
    warning_at (loc, 0, "assignment between incompatible pointer types");
}

static ex native_thunk (ex e, tree type, location_t loc);
static bool generic_void_fn_p (tree fntype);
static tree libd_redirect (tree fndecl);

/* Convert E to TYPE.  EXPLICIT_P for casts.  RAW_P when the target is a
   native pointer parameter of a GCC builtin.  */
static ex
convert_to (ex e, tree type, location_t loc, bool explicit_p)
{
  if (error_p (type))
    return err ();
  if (TREE_CODE (type) == POINTER_TYPE && !bad (e) && e.t
      && TREE_CODE (e.type) == ARRAY_TYPE && va_list_p (e.type))
    /* va_list array passed on as a native va_list parameter.  */
    return mk (fold_convert (type, array_first_addr (e.t, loc)), type);
  e = rvalue (e, loc);
  if (bad (e))
    return e;

  if (TZD_MPTR_P (type))
    {
      if (TZD_MPTR_P (e.type))
	{
	  if (TYPE_MAIN_VARIANT (e.type) == TYPE_MAIN_VARIANT (type))
	    {
	      e.type = type;
	      return e;
	    }
	  if (!explicit_p)
	    pointer_mismatch (type, e.type, loc);
	  parts (e);
	  return mk_parts (type, e.pa, e.pb, e.ps);
	}
      if (null_const_p (e))
	return mk_parts (type, null_pointer_node, null_pointer_node,
			 size_zero_node);
      if (fnptr_p (e.type) && !flag_tzd_kernel)
	{
	  tree fl = e.t;
	  STRIP_NOPS (fl);
	  if (const char *const *hl = fnptr_holds_list (fl))
	    {
	      /* (T *) s->FIELD of a fnptr_holds member (task #70): the
		 recorded data pointer, of type T (a char or void view takes
		 any).  */
	      tree tt = TZD_MPTR_TARGET (type);
	      unsigned long id = 0;
	      if (!byte_view_type_p (tt))
		{
		  if (!fnptr_holds_allows (hl, tt))
		    {
		      error_at (loc, "%<#pragma tzd fnptr_holds%> for this member "
				"does not list the type %qT", tt);
		      return err ();
		    }
		  id = fnptr_holds_id (tt);
		}
	      init_shadow_decls ();
	      mark_addressable_tree (fl);
	      tree c = build_call_expr_loc
		(loc, tzd_sh_load_typed_decl, 2,
		 fold_convert (const_ptr_type_node,
			       build_fold_addr_expr_loc (loc, fl)),
		 build_int_cstu (size_type_node, id));
	      return mk (build1 (VIEW_CONVERT_EXPR, mrec (type), c), type);
	    }
	}
      if (fnptr_p (e.type)
	  && (explicit_p || VOID_TYPE_P (TZD_MPTR_TARGET (type))))
	{
	  /* (void *) fn: the code address, no bytes reachable (reading code
	     through a data pointer traps).  Kernel mode: the kernel reads
	     and patches its own code (static calls, alternatives, jump
	     labels), so the address is bounded like an integer, by its
	     region (the kernel image; 要確認, task #42/#48).  */
	  if (flag_tzd_kernel)
	    return from_native (e.t, type, loc);
	  return mk_parts (type, fold_convert (mp_native (type), e.t),
			   null_pointer_node, size_zero_node);
	}
      if (INTEGRAL_TYPE_P (e.type))
	{
	  if (!explicit_p && tzd_extensions_enabled)
	    {
	      /* TzD (.d) is strict: an implicit integer/pointer conversion
		 is ambiguous code, so it is an error (write a cast).  */
	      error_at (loc, "making pointer from integer without a cast");
	      return err ();
	    }
	  if (!explicit_p)
	    warning_at (loc, 0, "making pointer from integer without a cast");
	  /* An integer made an object pointer is a real address: only kernel
	     mode may use one (task #79, spec 7).  Outside it, two forms stay:
	     an all-ones sentinel (MAP_FAILED, (char *) -1 of sbrk), and an
	     integer carried in a void pointer (thread and callback
	     arguments), which reaches no bytes either (an access through it,
	     after any cast, still traps).  Unevaluated operands (sizeof,
	     typeof: the kernel's __is_constexpr) make no address.  */
	  if (!flag_tzd_kernel && !unevaluated)
	    {
	      tree c = e.t;
	      STRIP_NOPS (c);
	      if (!VOID_TYPE_P (TZD_MPTR_TARGET (type))
		  && !(TREE_CODE (c) == INTEGER_CST && integer_all_onesp (c)))
		{
		  error_at (loc, "an integer converted to %qT is a real "
			    "address; only kernel mode (%<-ftzd-kernel%>) "
			    "uses real addresses", type);
		  return err ();
		}
	    }
	  /* No allocation behind it: any access traps (kernel mode: the
	     bounds of its memory region).  */
	  return from_native (e.t, type, loc);
	}
      error_at (loc, "cannot convert to a pointer type");
      return err ();
    }

  if (TREE_CODE (type) == POINTER_TYPE)
    {
      /* Native pointer: function pointers, or raw pointer parameters of
	 GCC builtins.  */
      if (fnptr_p (e.type) && fnptr_p (type)
	  && (fn_abi_mismatch_p (TREE_TYPE (e.type), TREE_TYPE (type))
	      /* Erasing any real signature to the generic void(*)(void) must
		 register it even with no pointer parameters (task #61).  */
	      || (generic_void_fn_p (TREE_TYPE (type))
		  && !generic_void_fn_p (TREE_TYPE (e.type)))))
	return native_thunk (e, type, loc);
      if (fnptr_p (e.type) || null_const_p (e) || INTEGRAL_TYPE_P (e.type))
	return mk (fold_convert (type, e.t), type);
      if (null_mptr_p (e))
	return mk (build_int_cst (type, 0), type);
      if (TZD_MPTR_P (e.type))
	{
	  if (fnptr_p (type) && static_init_depth == 0 && current_function_decl)
	    {
	      /* A data pointer made a function pointer: keep its record for
		 a store into a fnptr_holds member (task #70).  */
	      tree v = save_expr (value (e));
	      ex ev = mk (v, e.type);
	      parts (ev);
	      ex r = mk (fold_convert (type, ev.pa), type);
	      r.fdata = v;
	      r.fdtype = TZD_MPTR_TARGET (e.type);
	      return r;
	    }
	  /* Raw-C boundary (host libc, provisional #14): pass pointer().  */
	  parts (e);
	  ex r = mk (fold_convert (type, e.pa), type);
	  if (fnptr_p (type))
	    r.fdtype = TZD_MPTR_TARGET (e.type);
	  return r;
	}
      if (TREE_CODE (e.type) == POINTER_TYPE)
	{
	  return mk (fold_convert (type, e.t), type);
	}
      error_at (loc, "incompatible type for pointer");
      return err ();
    }

  if (VOID_TYPE_P (type))
    return mk (value_void (e), void_type_node);

  if (INTEGRAL_TYPE_P (type))
    {
      if (TZD_MPTR_P (e.type))
	{
	  parts (e);
	  if (TREE_CODE (type) == BOOLEAN_TYPE)
	    return mk (fold_build2_loc (loc, NE_EXPR, type, e.pa,
					null_pointer_node), type);
	  if (!explicit_p && tzd_extensions_enabled)
	    {
	      error_at (loc, "making integer from pointer without a cast");
	      return err ();
	    }
	  if (!explicit_p)
	    warning_at (loc, 0, "making integer from pointer without a cast");
	  return mk (fold_convert (type, e.pa), type);
	}
      if (fnptr_p (e.type))
	return mk (fold_convert (type, e.t), type);
      if (arith_p (e.type))
	return mk (convert (type, e.t), type);
      error_at (loc, "incompatible type for conversion to integer");
      return err ();
    }

  if (SCALAR_FLOAT_TYPE_P (type))
    {
      if (arith_p (e.type))
	return mk (convert (type, e.t), type);
      error_at (loc, "incompatible type for conversion to floating type");
      return err ();
    }

  if (RECORD_OR_UNION_TYPE_P (type))
    {
      if (TYPE_MAIN_VARIANT (type) == TYPE_MAIN_VARIANT (e.type))
	{
	  e.type = type;
	  return e;
	}
      if (explicit_p && TREE_CODE (type) == UNION_TYPE
	  && !has_slots_p (type))
	/* Cast to a union (GNU C): the union with the member of the
	   operand's type set (ibs.c's (union ibs_fetch_ctl) config).  A union
	   holding pointers is not converted this way.  */
	for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
	  if (TREE_CODE (f) == FIELD_DECL && !DECL_BIT_FIELD (f)
	      && TYPE_MAIN_VARIANT (TREE_TYPE (f))
		 == TYPE_MAIN_VARIANT (e.type))
	    {
	      tree c = build_constructor_single (type, f, value (e));
	      TREE_CONSTANT (c) = TREE_CONSTANT (CONSTRUCTOR_ELT (c, 0)->value);
	      return mk (c, type);
	    }
      error_at (loc, "incompatible types in conversion");
      return err ();
    }

  error_at (loc, "conversion to this type is not supported");
  return err ();
}

/* The generic dispatch-table function type void(void) (OpenSSL stores
   functions as void(*)(void); task #61).  */
static bool
generic_void_fn_p (tree fntype)
{
  return fntype && TREE_CODE (fntype) == FUNCTION_TYPE
	 && VOID_TYPE_P (TREE_TYPE (fntype))
	 && TYPE_ARG_TYPES (fntype)
	 && TREE_VALUE (TYPE_ARG_TYPES (fntype)) == void_type_node;
}

/* ---- Function-pointer interface identity (task #61, resolving #22) ----
   A stable hash of a function signature, computed the same way at the point
   a named function is erased to the generic void(*)(void) (tzd_fn_reg) and
   at __tzd_fncast (tzd_fn_check).  Managed and raw pointers encode alike, so
   the two sides agree whatever the pointer representation; recursion is
   depth-limited and pointee structs are named, so distinct interfaces
   almost never collide (a collision only lets an equally-shaped wrong
   function pass, like a CFI type id).  */
static void
tzd_enc_type (tree t, char *buf, size_t cap, size_t *len, int depth)
{
  if (!t || *len + 24 >= cap)
    return;
  t = TYPE_MAIN_VARIANT (t);
  char *p = buf + *len;
  if (depth <= 0)
    {
      buf[(*len)++] = 'X';
      return;
    }
  /* The id is at the System V ABI class level: what decides how an argument
     or result is passed/returned, not its C type.  Integers, enums, bools
     and pointers are one class ('I', an integer register / RAX); float and
     double are another ('R', an SSE register).  So an int-returning
     interface implemented by a long-returning function (OpenSSL's dispatch
     relies on this -- ABI-identical on x86-64), or void* vs a typed pointer,
     have the same id, while arity, integer-vs-float and struct differences
     (real ABI breaks) still differ.  Type-level confusion within a class is
     not a calling-convention break and any out-of-bounds use is still caught
     by the bounds checks (task #61).  */
  if (TZD_MPTR_P (t) || TREE_CODE (t) == POINTER_TYPE)
    {
      buf[(*len)++] = 'I';
      return;
    }
  int n = 0;
  switch (TREE_CODE (t))
    {
    case INTEGER_TYPE: case ENUMERAL_TYPE: case BOOLEAN_TYPE:
      buf[(*len)++] = 'I';
      return;
    case REAL_TYPE:
      buf[(*len)++] = 'R';
      return;
    case VOID_TYPE:
      buf[(*len)++] = 'v';
      return;
    case RECORD_TYPE: case UNION_TYPE:
      {
	tree nm = TYPE_NAME (t);
	if (nm && TREE_CODE (nm) == TYPE_DECL)
	  nm = DECL_NAME (nm);
	if (nm && TREE_CODE (nm) == IDENTIFIER_NODE)
	  n = snprintf (p, cap - *len, "S%s", IDENTIFIER_POINTER (nm));
	else
	  n = snprintf (p, cap - *len, "S%ld",
			(long) (TYPE_SIZE (t)
				? TREE_INT_CST_LOW (TYPE_SIZE (t)) : 0));
	break;
      }
    case FUNCTION_TYPE:
      {
	buf[(*len)++] = 'F';
	tzd_enc_type (TREE_TYPE (t), buf, cap, len, depth - 1);
	if (*len + 1 < cap)
	  buf[(*len)++] = '(';
	for (tree a = TYPE_ARG_TYPES (t);
	     a && TREE_VALUE (a) != void_type_node; a = TREE_CHAIN (a))
	  tzd_enc_type (TREE_VALUE (a), buf, cap, len, depth - 1);
	if (*len + 1 < cap)
	  buf[(*len)++] = ')';
	return;
      }
    default:
      n = snprintf (p, cap - *len, "U%d", (int) TREE_CODE (t));
      break;
    }
  if (n < 0)
    return;
  *len += ((size_t) n < cap - *len) ? (size_t) n : (cap - *len - 1);
}

/* The interface id of a function type (or pointer-to-function type).  */
static unsigned long
tzd_fn_interface_id (tree fntype)
{
  if (fntype && (TZD_MPTR_P (fntype) || TREE_CODE (fntype) == POINTER_TYPE))
    fntype = TZD_MPTR_P (fntype) ? TZD_MPTR_TARGET (fntype)
				 : TREE_TYPE (fntype);
  char buf[512];
  size_t len = 0;
  tzd_enc_type (fntype, buf, sizeof buf, &len, 6);
  unsigned long h = 1469598103934665603UL;
  for (size_t i = 0; i < len; i++)
    {
      h ^= (unsigned char) buf[i];
      h *= 1099511628211UL;
    }
  return h;
}

/* A function whose pointer ABI (managed vs. raw pointers) differs from the
   function pointer type it is converted to -- e.g. a Managed C comparator
   handed to the host libc's qsort, or a host function stored in a Managed
   C function pointer.  Emit a static thunk of the target type that
   converts each argument (raw -> managed: bounded below only; managed ->
   raw: pointer()) and the result, then calls the function (task #22).  */
static ex transparent_union_arg (ex a, tree ut, location_t loc);

static ex
native_thunk (ex e, tree type, location_t loc)
{
  tree fn = e.t;
  STRIP_NOPS (fn);
  tree ntype = TREE_TYPE (type);
  if (fn && TREE_CODE (fn) == COND_EXPR)
    {
      /* cond ? f : g (busybox's `lf = soft ? symlink : link`): a thunk for
	 each arm.  */
      ex a = native_thunk (mk (TREE_OPERAND (fn, 1),
			       TREE_TYPE (TREE_OPERAND (fn, 1))), type, loc);
      ex b = native_thunk (mk (TREE_OPERAND (fn, 2),
			       TREE_TYPE (TREE_OPERAND (fn, 2))), type, loc);
      if (bad (a) || bad (b))
	return err ();
      return mk (build3_loc (loc, COND_EXPR, type, TREE_OPERAND (fn, 0),
			     a.t, b.t), type);
    }
  /* Erasing a function to the generic void(*)(void) of a dispatch table
     (OpenSSL's OSSL_DISPATCH): the real address must be preserved so that
     __tzd_fncast can recover the true type and call it with its arguments
     -- a void(void) thunk would drop them.  So no thunk: register the
     source's real interface id and reinterpret the address to the generic
     type.  __tzd_fncast later checks the id matches (task #61).  */
  if (generic_void_fn_p (ntype))
    {
      unsigned long id = tzd_fn_interface_id (e.type);
      tree idc = build_int_cstu (long_unsigned_type_node, id);
      /* In a static initializer (a dispatch table) a tzd_fn_reg call is not
	 a constant: queue the (address, id) for startup registration via
	 section tzd_fninit, and yield the plain constant address.  */
      if (static_init_depth > 0)
	{
	  vec_safe_push (fninit_list,
			 fold_convert (const_ptr_type_node, value (e)));
	  vec_safe_push (fninit_list, idc);
	  return mk (fold_convert (type, value (e)), type);
	}
      init_shadow_decls ();
      tree src = save_expr (value (e));
      tree reg = build_call_expr_loc (loc, tzd_fn_reg_decl, 2,
				      fold_convert (const_ptr_type_node, src),
				      idc);
      return mk (build2 (COMPOUND_EXPR, type, reg, fold_convert (type, src)),
		 type);
    }
  if (TREE_CODE (fn) != ADDR_EXPR
      || TREE_CODE (TREE_OPERAND (fn, 0)) != FUNCTION_DECL
      || stdarg_p (ntype) || !prototype_p (ntype))
    {
      /* Calling through the converted pointer would pass raw pointers
	 where managed ones are expected (or the reverse): no thunk, no
	 conversion (strict, task #16).  */
      error_at (loc, "function pointer with a different pointer ABI "
		"(managed vs. host) cannot be converted here: only a named "
		"function with a prototype and no variable arguments gets a "
		"thunk (task #22)");
      return err ();
    }
  tree fndecl = TREE_OPERAND (fn, 0);
  /* A host function libc2d replaces (pcre's `pcre_malloc = malloc`): the
     replacement already has the managed ABI, so its address is used, as a
     call would be redirected.  */
  if (tree r = libd_redirect (fndecl))
    if (!fn_abi_mismatch_p (TREE_TYPE (r), ntype))
      {
	TREE_USED (r) = 1;
	return mk (fold_convert (type, build_fold_addr_expr_loc (loc, r)),
		   type);
      }
  for (unsigned i = 0; thunk_cache && i + 2 < thunk_cache->length (); i += 3)
    if ((*thunk_cache)[i] == fndecl
	&& TYPE_MAIN_VARIANT ((*thunk_cache)[i + 1]) == TYPE_MAIN_VARIANT (ntype))
      return mk (build_fold_addr_expr_loc (loc, (*thunk_cache)[i + 2]), type);

  tree utype = TREE_TYPE (fndecl);
  /* One thunk per (function, pointer-ABI pattern of the target type) in
     the whole program for a function with external linkage (user mode):
     a COMDAT named after both, so every file taking `read` as a Managed C
     function pointer gets the same address and `f == read` holds across
     files (OpenSSH's atomicio6: `f == read ? POLLIN : POLLOUT`, task #70).
     The thunk's code depends only on that pattern.  A static function (or
     kernel mode) keeps a thunk local to the file.  */
  bool shared = TREE_PUBLIC (fndecl) && !flag_tzd_kernel;
  static int counter;
  char *name;
  if (shared)
    {
      char key[256];
      size_t k = 0;
      auto cls = [] (tree t) -> char {
	if (TZD_MPTR_P (t)) return 'M';
	if (TREE_CODE (t) == POINTER_TYPE) return 'P';
	if (VOID_TYPE_P (t)) return 'v';
	if (SCALAR_FLOAT_TYPE_P (t)) return 'R';
	if (INTEGRAL_TYPE_P (t)) return 'I';
	return 'S';
      };
      key[k++] = cls (TREE_TYPE (ntype));
      key[k++] = '_';
      for (tree a = TYPE_ARG_TYPES (ntype);
	   a && TREE_VALUE (a) != void_type_node && k < sizeof key - 1;
	   a = TREE_CHAIN (a))
	key[k++] = cls (TREE_VALUE (a));
      key[k] = 0;
      name = xasprintf ("__tzd_thunk.%s.%s",
			IDENTIFIER_POINTER (DECL_NAME (fndecl)), key);
    }
  else
    name = xasprintf ("__tzd_thunk.%s.%d",
		      IDENTIFIER_POINTER (DECL_NAME (fndecl)), counter++);
  tree thid = get_identifier (name);
  free (name);
  if (shared)
    for (unsigned i = 0; thunk_cache && i + 2 < thunk_cache->length (); i += 3)
      if (DECL_NAME ((*thunk_cache)[i + 2]) == thid)
	return mk (fold_convert (type, build_fold_addr_expr_loc
				   (loc, (*thunk_cache)[i + 2])), type);
  tree th = build_decl (loc, FUNCTION_DECL, thid, ntype);
  TREE_STATIC (th) = 1;
  TREE_PUBLIC (th) = shared;
  if (shared)
    {
      DECL_VISIBILITY (th) = VISIBILITY_HIDDEN;
      DECL_VISIBILITY_SPECIFIED (th) = 1;
      make_decl_one_only (th, DECL_ASSEMBLER_NAME (th));
    }
  DECL_ARTIFICIAL (th) = 1;
  DECL_CONTEXT (th) = NULL_TREE;
  TREE_USED (th) = 1;

  tree saved_fn = current_function_decl;
  current_function_decl = th;
  tree rtype = TREE_TYPE (ntype);
  tree res = build_decl (loc, RESULT_DECL, NULL_TREE, rtype);
  DECL_ARTIFICIAL (res) = 1;
  DECL_IGNORED_P (res) = 1;
  DECL_CONTEXT (res) = th;
  DECL_RESULT (th) = res;

  tree parms = NULL_TREE, *tail = &parms;
  vec<tree> args = vNULL;
  tree np = TYPE_ARG_TYPES (ntype), up = TYPE_ARG_TYPES (utype);
  bool ok = true;
  for (; np && TREE_VALUE (np) != void_type_node; np = TREE_CHAIN (np))
    {
      tree pt = TREE_VALUE (np);
      tree p = build_decl (loc, PARM_DECL, NULL_TREE, pt);
      DECL_ARG_TYPE (p) = pt;
      DECL_CONTEXT (p) = th;
      DECL_ARTIFICIAL (p) = 1;
      TREE_USED (p) = 1;
      *tail = p;
      tail = &DECL_CHAIN (p);
      tree want = pt;
      if (up && TREE_VALUE (up) != void_type_node)
	{
	  want = TREE_VALUE (up);
	  up = TREE_CHAIN (up);
	}
      ex a = (TREE_CODE (want) == UNION_TYPE
	      && TYPE_TRANSPARENT_AGGR (TYPE_MAIN_VARIANT (want)))
	     /* A host transparent union (glibc's __SOCKADDR_ARG).  */
	     ? transparent_union_arg (mk (p, pt), want, loc)
	     : convert_to (mk (p, pt), want, loc, true);
      if (bad (a))
	ok = false;
      else
	args.safe_push (value (a));
    }
  DECL_ARGUMENTS (th) = parms;
  tree call = build_call_array_loc (loc, TREE_TYPE (utype), fn,
				    args.length (), args.address ());
  args.release ();
  tree body;
  if (VOID_TYPE_P (rtype))
    body = call;
  else
    {
      ex r = convert_to (mk (call, TREE_TYPE (utype)), rtype, loc, true);
      if (bad (r))
	ok = false;
      body = build1 (RETURN_EXPR, void_type_node,
		     build2 (MODIFY_EXPR, rtype, res,
			     bad (r) ? error_mark_node : value (r)));
    }
  tree block = make_node (BLOCK);
  BLOCK_SUPERCONTEXT (block) = th;
  TREE_USED (block) = 1;
  DECL_INITIAL (th) = block;
  tree bind = build3_loc (loc, BIND_EXPR, void_type_node, NULL_TREE, body,
			  block);
  TREE_SIDE_EFFECTS (bind) = 1;
  DECL_SAVED_TREE (th) = bind;
  current_function_decl = saved_fn;
  if (!ok)
    return err ();

  vec_safe_push (thunk_cache, fndecl);
  vec_safe_push (thunk_cache, ntype);
  vec_safe_push (thunk_cache, th);
  if (!seen_error ())
    {
      struct function *saved_cfun = cfun;
      allocate_struct_function (th, false);
      cfun->function_end_locus = loc;
      set_cfun (saved_cfun);
      cgraph_node::finalize_function (th, true);
    }
  return mk (build_fold_addr_expr_loc (loc, th), type);
}

/* Truth value of E as an int-typed tree; NEGATE gives !E.  */
static bool slot_lv_p (const ex &e);
static tree slot_raw_addr (tree l, location_t loc);

static tree
truth (ex e, location_t loc, bool negate = false)
{
  if (!bad (e) && slot_lv_p (e))
    {
      /* if (p->f): a null test of the slot's address only.  A heap slot
	 must have been written, like any read (task #8).  */
      tree c = fold_build2_loc (loc, negate ? EQ_EXPR : NE_EXPR,
				integer_type_node, slot_raw_addr (e.t, loc),
				null_pointer_node);
      if (tree addr = mem_lvalue_addr (e.t, loc))
	if (tree chk = hp_access (addr, size_int (8), true, loc))
	  {
	    c = save_expr (c);
	    c = build2 (COMPOUND_EXPR, integer_type_node,
			build2 (COMPOUND_EXPR, void_type_node,
				fold_convert (void_type_node, c), chk), c);
	  }
      return c;
    }
  e = rvalue (e, loc);
  if (bad (e))
    return error_mark_node;
  enum tree_code code = negate ? EQ_EXPR : NE_EXPR;
  if (TZD_MPTR_P (e.type))
    {
      parts (e);
      return fold_build2_loc (loc, code, integer_type_node, e.pa,
			      build_int_cst (TREE_TYPE (e.pa), 0));
    }
  if (TREE_CODE (e.type) == POINTER_TYPE || arith_p (e.type))
    return fold_build2_loc (loc, code, integer_type_node, e.t,
			    build_zero_cst (TREE_TYPE (e.t)));
  error_at (loc, "used a value that cannot be converted to a truth value");
  return error_mark_node;
}

static ex
promote (ex e, location_t loc)
{
  e = rvalue (e, loc);
  if (bad (e) || !arith_p (e.type))
    return e;
  tree t = promote_type (e.type);
  return mk (convert (t, e.t), t);
}

/* ---- binary operators ---- */

static ex binop_1 (const char *op, ex a, ex b, location_t loc);

static ex
binop (const char *op, ex a, ex b, location_t loc)
{
  bool ice = a.ice && b.ice;
  ex r = binop_1 (op, a, b, loc);
  r.ice = ice && !bad (r);
  return r;
}

/* The raw 8-byte address held in managed-pointer slot lvalue E, read
   without its shadow entry: only for testing it against null (p->f ==
   NULL), which needs no bounds and grants no access.  OpenSSH memcpy's
   a struct of pointers received from another process and tests which
   fields were set before overwriting them (monitor_wrap.c
   mm_decode_activate_server_options); the stale address is never
   dereferenced.  Anything else done with such a value still traps.  */
static bool
slot_lv_p (const ex &e)
{
  return e.t && e.lv && !e.nullbase && TZD_MSLOT_P (TREE_TYPE (e.t));
}

static tree
slot_raw_addr (tree l, location_t loc)
{
  mark_addressable_tree (l);
  tree addr = build_fold_addr_expr_with_type_loc (loc, l,
						  build_pointer_type
						    (TREE_TYPE (l)));
  tree pp = build_pointer_type (ptr_type_node);
  return build_simple_mem_ref_loc (loc, fold_convert (pp, addr));
}

static ex
binop_1 (const char *op, ex a, ex b, location_t loc)
{
  if ((strcmp (op, "==") == 0 || strcmp (op, "!=") == 0)
      && (slot_lv_p (a) || slot_lv_p (b)))
    {
      ex s = slot_lv_p (a) ? a : b, n = slot_lv_p (a) ? b : a;
      ex nr = rvalue (n, loc);
      if (!bad (nr) && nr.ice
	  && (null_const_p (nr)
	      || (TZD_MPTR_P (nr.type) && null_mptr_p (nr))))
	return mk (fold_build2_loc (loc, op[0] == '=' ? EQ_EXPR : NE_EXPR,
				    integer_type_node,
				    slot_raw_addr (s.t, loc),
				    null_pointer_node),
		   integer_type_node);
    }
  a = rvalue (a, loc);
  b = rvalue (b, loc);
  if (bad (a) || bad (b))
    return err ();
  bool ap = TZD_MPTR_P (a.type), bp = TZD_MPTR_P (b.type);

  if (strcmp (op, "&&") == 0 || strcmp (op, "||") == 0)
    return mk (fold_build2_loc (loc, op[0] == '&' ? TRUTH_ANDIF_EXPR
				: TRUTH_ORIF_EXPR, integer_type_node,
				truth (a, loc), truth (b, loc)),
	       integer_type_node);

  if (strcmp (op, "+") == 0)
    {
      if (ap && INTEGRAL_TYPE_P (b.type))
	return ptr_add (a, b.t, false, loc);
      if (bp && INTEGRAL_TYPE_P (a.type))
	return ptr_add (b, a.t, false, loc);
    }
  if (strcmp (op, "-") == 0)
    {
      if (ap && bp)
	return ptr_diff (a, b, loc);
      if (ap && INTEGRAL_TYPE_P (b.type))
	return ptr_add (a, b.t, true, loc);
    }

  enum tree_code cmp = ERROR_MARK;
  if (strcmp (op, "==") == 0) cmp = EQ_EXPR;
  else if (strcmp (op, "!=") == 0) cmp = NE_EXPR;
  else if (strcmp (op, "<") == 0) cmp = LT_EXPR;
  else if (strcmp (op, ">") == 0) cmp = GT_EXPR;
  else if (strcmp (op, "<=") == 0) cmp = LE_EXPR;
  else if (strcmp (op, ">=") == 0) cmp = GE_EXPR;

  /* f == read with f a Managed C function pointer and read a host function
     (different pointer ABI): compare the address f would hold, the thunk
     (task #70), not read's raw address.  */
  if ((cmp == EQ_EXPR || cmp == NE_EXPR) && fnptr_p (a.type)
      && fnptr_p (b.type)
      && fn_abi_mismatch_p (TREE_TYPE (a.type), TREE_TYPE (b.type)))
    {
      tree ta = a.t, tb = b.t;
      STRIP_NOPS (ta);
      STRIP_NOPS (tb);
      bool da = TREE_CODE (ta) == ADDR_EXPR
		&& TREE_CODE (TREE_OPERAND (ta, 0)) == FUNCTION_DECL;
      bool db = TREE_CODE (tb) == ADDR_EXPR
		&& TREE_CODE (TREE_OPERAND (tb, 0)) == FUNCTION_DECL;
      if (db && !da)
	b = native_thunk (b, a.type, loc);
      else if (da && !db)
	a = native_thunk (a, b.type, loc);
      else
	{
	  error_at (loc, "comparison of function pointers with different "
		    "pointer ABIs (managed vs. host)");
	  return err ();
	}
      if (bad (a) || bad (b))
	return err ();
    }
  if (cmp != ERROR_MARK && (ap || bp || fnptr_p (a.type)
			    || fnptr_p (b.type)))
    {
      tree x, y;
      if (ap)
	{
	  parts (a);
	  x = fold_convert (ptr_type_node, a.pa);
	}
      else if (null_const_p (a) || TREE_CODE (a.type) == POINTER_TYPE)
	x = fold_convert (ptr_type_node, a.t);
      else
	{
	  error_at (loc, "comparison between pointer and integer");
	  return err ();
	}
      if (bp)
	{
	  parts (b);
	  y = fold_convert (ptr_type_node, b.pa);
	}
      else if (null_const_p (b) || TREE_CODE (b.type) == POINTER_TYPE)
	y = fold_convert (ptr_type_node, b.t);
      else
	{
	  error_at (loc, "comparison between pointer and integer");
	  return err ();
	}
      return mk (fold_build2_loc (loc, cmp, integer_type_node, x, y),
		 integer_type_node);
    }

  if ((op[0] == '+' || op[0] == '-') && op[1] == 0
      && ((fnptr_p (a.type) && INTEGRAL_TYPE_P (b.type))
	  || (op[0] == '+' && fnptr_p (b.type) && INTEGRAL_TYPE_P (a.type))))
    {
      /* Function pointer +/- integer (GNU C, bytes: the kernel's
	 prog->bpf_func + offset).  Function pointers carry no bounds and an
	 integer converts to one anyway, so this reaches nothing new.  */
      ex fp = fnptr_p (a.type) ? a : b, n = fnptr_p (a.type) ? b : a;
      tree off = fold_convert (sizetype, n.t);
      if (op[0] == '-')
	off = fold_build1_loc (loc, NEGATE_EXPR, sizetype, off);
      return mk (fold_build_pointer_plus_loc (loc, fp.t, off), fp.type);
    }

  if (!arith_p (a.type) || !arith_p (b.type))
    {
      error_at (loc, "invalid operands to binary %s", op);
      return err ();
    }

  if (cmp != ERROR_MARK)
    {
      tree ct = common_arith (a.type, b.type);
      return mk (fold_build2_loc (loc, cmp, integer_type_node,
				  convert (ct, a.t), convert (ct, b.t)),
		 integer_type_node);
    }

  bool shift = strcmp (op, "<<") == 0 || strcmp (op, ">>") == 0;
  tree ct = shift ? promote_type (a.type) : common_arith (a.type, b.type);
  bool flt = SCALAR_FLOAT_TYPE_P (ct);
  enum tree_code code;
  switch (op[0])
    {
    case '+': code = PLUS_EXPR; break;
    case '-': code = MINUS_EXPR; break;
    case '*': code = MULT_EXPR; break;
    case '/': code = flt ? RDIV_EXPR : TRUNC_DIV_EXPR; break;
    case '%': code = TRUNC_MOD_EXPR; break;
    case '&': code = BIT_AND_EXPR; break;
    case '|': code = BIT_IOR_EXPR; break;
    case '^': code = BIT_XOR_EXPR; break;
    case '<': code = LSHIFT_EXPR; break;
    case '>': code = RSHIFT_EXPR; break;
    default: gcc_unreachable ();
    }
  if (flt && (code == TRUNC_MOD_EXPR || code == BIT_AND_EXPR
	      || code == BIT_IOR_EXPR || code == BIT_XOR_EXPR || shift))
    {
      error_at (loc, "invalid operands to binary %s", op);
      return err ();
    }
  tree y = shift ? convert (promote_type (b.type), b.t) : convert (ct, b.t);
  return mk (fold_build2_loc (loc, code, ct, convert (ct, a.t), y), ct);
}

/* ------------------------------------------------------------------ */
/* Function context.  */

struct fn_ctx {
  tree decl;
  vec<tree> break_labels;
  vec<tree> continue_labels;
  vec<tree> labels;            /* identifier, LABEL_DECL pairs */
  vec<tree> switch_types;
  vec<tree> block_chains;      /* per open compound: sub-BLOCK chain */
  vec<tree> addr_taken;        /* identifiers that appear after '&' */
  vec<tree> local_labels;      /* __label__: identifier, LABEL_DECL pairs */
  tree sv_ret;                 /* hidden result pointer (sv), or NULL */
  tree lang_ret;               /* declared result type */
  /* A static initializer holds &&label of this function (cfun's
     has_forced_label_in_static: never inlined or cloned away).  */
  bool label_in_static;
  /* A library function (declared in a system header: libc2d's Managed C
     parts): it writes with its caller's permission, so its own stores
     are not checked against write groups; its callers' pointer arguments
     are (task #79).  */
  bool delegate;
};

static fn_ctx *cur_fn;
static tree *cur_stmts;

/* ------------------------------------------------------------------ */
/* Write groups (task #79, spec 5/6; user space first).

   `// @tzd group=G writer=F1,F2` on a variable with static storage puts
   it in section tzd_grp and lists it in section tzd_grpd (libc2d
   group.c).  A store whose address may lie in tzd_grp -- through a
   pointer, or into an extern variable -- tests the address against the
   section (two link-time constants) and, inside, calls tzd_grp_store
   with the name of the writing function.  A store that names a grouped
   variable of this unit is decided here: a function not listed is a
   compile error.  A pointer argument that a library function may write
   through (a non-const pointer parameter) is tested at the call with the
   caller's name: the library writes with its caller's permission.  */

static GTY(()) tree grp_start_decl;
static GTY(()) tree grp_stop_decl;
static GTY(()) tree grp_store_decl;
static GTY(()) vec<tree, va_gc> *grp_vars;

static void
init_grp_decls (void)
{
  if (grp_store_decl)
    return;
  auto sym = [] (const char *name)
    {
      tree d = build_decl (BUILTINS_LOCATION, VAR_DECL, get_identifier (name),
			   build_array_type (char_type_node, NULL_TREE));
      TREE_PUBLIC (d) = 1;
      DECL_EXTERNAL (d) = 1;
      DECL_ARTIFICIAL (d) = 1;
      TREE_ADDRESSABLE (d) = 1;
      return d;
    };
  grp_start_decl = sym ("__start_tzd_grp");
  grp_stop_decl = sym ("__stop_tzd_grp");
  grp_store_decl = libd_decl ("tzd_grp_store",
			      build_function_type_list (void_type_node,
							const_ptr_type_node,
							const_ptr_type_node,
							NULL_TREE));
}

/* Are stores in the current function checked against write groups?  */
static bool
grp_active_p (void)
{
  return !flag_tzd_kernel && current_function_decl && cur_fn
	 && !cur_fn->delegate && !static_init_depth && !unevaluated;
}

/* The group of variable DECL: a TREE_LIST (group name, writers) of
   STRING_CSTs, or NULL_TREE.  */
static tree
grp_of (tree decl)
{
  if (!VAR_P (decl))
    return NULL_TREE;
  tree a = lookup_attribute ("tzd group", DECL_ATTRIBUTES (decl));
  return a ? TREE_VALUE (a) : NULL_TREE;
}

/* Is function FN one of the writers of group G?  */
static bool
grp_writer_p (tree g, tree fn)
{
  if (!fn || !DECL_NAME (fn) || !TREE_VALUE (g))
    return false;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (fn));
  size_t len = strlen (name);
  const char *s = TREE_STRING_POINTER (TREE_VALUE (g));
  while (*s)
    {
      const char *e = strchr (s, ',');
      size_t n = e ? (size_t) (e - s) : strlen (s);
      if (n == len && memcmp (s, name, n) == 0)
	return true;
      if (!e)
	break;
      s = e + 1;
    }
  return false;
}

/* The test of a store at native address ADDR by the current function.  */
static tree
grp_guard_addr (tree addr, location_t loc)
{
  init_grp_decls ();
  tree a = save_expr (fold_convert (size_type_node, addr));
  tree lo = fold_convert (size_type_node,
			  build_fold_addr_expr_loc (loc, grp_start_decl));
  tree hi = fold_convert (size_type_node,
			  build_fold_addr_expr_loc (loc, grp_stop_decl));
  tree in = fold_build2 (LT_EXPR, integer_type_node,
			 fold_build2 (MINUS_EXPR, size_type_node, a, lo),
			 fold_build2 (MINUS_EXPR, size_type_node, hi, lo));
  const char *fn = IDENTIFIER_POINTER (DECL_NAME (current_function_decl));
  tree call = build_call_expr_loc (loc, grp_store_decl, 2,
				   fold_convert (const_ptr_type_node, a),
				   build_string_literal (strlen (fn) + 1, fn));
  return build3 (COND_EXPR, void_type_node, in, call, build_empty_stmt (loc));
}

/* The write-group test of a store into lvalue *LP, or NULL_TREE; *LP may
   be stabilized (its pointer evaluated once, by the test).  A store that
   names a grouped variable of this unit by a function not among its
   writers is a compile error here.  */
static tree
grp_store_guard (tree *lp, location_t loc)
{
  if (!grp_active_p ())
    return NULL_TREE;
  tree l = *lp;
  tree b = l;
  while (handled_component_p (b))
    b = TREE_OPERAND (b, 0);
  if (DECL_P (b))
    {
      if (!VAR_P (b))
	return NULL_TREE;
      if (tree g = grp_of (b))
	{
	  if (!grp_writer_p (g, current_function_decl))
	    error_at (loc, "%qD writes %qD of group %qs, but is not one of "
		      "its writers (task #79)", current_function_decl, b,
		      TREE_STRING_POINTER (TREE_PURPOSE (g)));
	  return NULL_TREE;
	}
      if (!DECL_EXTERNAL (b))
	return NULL_TREE;   /* defined here without a group */
      TREE_ADDRESSABLE (b) = 1;
      return grp_guard_addr (build_fold_addr_expr_loc (loc, b), loc);
    }
  if (TREE_CODE (b) != INDIRECT_REF && TREE_CODE (b) != MEM_REF)
    return NULL_TREE;
  tree addr = mem_lvalue_addr (l, loc);
  if (!addr)
    {
      /* A bit-field or an operand with side effects: test the pointer
	 the lvalue is reached through (its object holds the bytes).  */
      *lp = l = stabilize_reference (l);
      tree bb = l;
      while (handled_component_p (bb))
	bb = TREE_OPERAND (bb, 0);
      addr = TREE_OPERAND (bb, 0);
    }
  return grp_guard_addr (addr, loc);
}

/* Prepend GUARD (or nothing) to the value of E.  */
static ex
grp_prepend (tree guard, ex e)
{
  if (!guard || bad (e))
    return e;
  tree v = value (e);
  tree r = build2 (COMPOUND_EXPR, TREE_TYPE (v), guard, v);
  TREE_SIDE_EFFECTS (r) = 1;
  ex out = mk (r, e.type);
  return out;
}

/* Apply annotation AN to variable VAR (parse_declaration).  */
static void
grp_apply_annot (tzd_annot *an, tree var, location_t loc, bool file_scope)
{
  const char *group = NULL, *writers = NULL;
  for (unsigned i = 0; i < an->n; i++)
    {
      if (strcmp (an->key[i], "group") == 0 && an->val[i] && !group)
	group = an->val[i];
      else if (strcmp (an->key[i], "writer") == 0 && an->val[i] && !writers)
	writers = an->val[i];
      else
	{
	  error_at (an->loc, "unknown or repeated key %qs in a %<@tzd%> "
		    "annotation on a variable (task #79)", an->key[i]);
	  return;
	}
    }
  if (!group)
    {
      error_at (an->loc, "%<writer=%> needs %<group=%> (task #79)");
      return;
    }
  for (const char *q = group; *q; q++)
    if (!ISALNUM (*q) && *q != '_')
      {
	error_at (an->loc, "group name %qs: letters, digits and %<_%> only",
		  group);
	return;
      }
  if (writers)
    for (const char *q = writers; *q; q++)
      if (!ISALNUM (*q) && *q != '_'
	  && !(*q == ',' && q != writers && q[1] && q[1] != ','))
	{
	  error_at (an->loc, "writers %qs: function names separated by "
		    "%<,%>", writers);
	  return;
	}
  if (!VAR_P (var) || !TREE_STATIC (var) || DECL_EXTERNAL (var)
      || DECL_THREAD_LOCAL_P (var))
    {
      error_at (loc, "%<group=%> applies to the definition of a variable "
		"with static storage (task #79)");
      return;
    }
  if (TYPE_READONLY (TREE_TYPE (var)))
    {
      error_at (loc, "%qD is const: no one writes it, it needs no group",
		var);
      return;
    }
  if (flag_tzd_kernel)
    {
      sorry_at (loc, "write groups in kernel mode (task #79, not yet)");
      return;
    }
  if (flag_tzd_delegate_unit)
    {
      error_at (loc, "a %<-ftzd-delegate-unit%> unit cannot declare a write "
		"group (task #79)");
      return;
    }
  (void) file_scope;
  tree g = build_tree_list (build_string (strlen (group) + 1, group),
			    writers ? build_string (strlen (writers) + 1,
						    writers)
				    : NULL_TREE);
  DECL_ATTRIBUTES (var) = tree_cons (get_identifier ("tzd group"), g,
				     DECL_ATTRIBUTES (var));
  set_decl_section_name (var, "tzd_grp");
  /* Kept even if unused here: the descriptor refers to it.  */
  DECL_PRESERVE_P (var) = 1;
  TREE_USED (var) = 1;
  if (DECL_ALIGN (var) < 128)
    SET_DECL_ALIGN (var, 128);
  vec_safe_push (grp_vars, var);
}

/* Section tzd_grpd: {address, "G:F1,F2", size} for each grouped variable
   of the unit, in the 24-byte record layout (libc2d group.c).  */
static void
emit_grp_table (void)
{
  unsigned n = vec_safe_length (grp_vars);
  if (n == 0)
    return;
  tree et = tzd_mrec_type (void_type_node);   /* {ptr, ptr, size_t} */
  tree at = build_array_type (et, build_index_type (size_int (n - 1)));
  vec<constructor_elt, va_gc> *v = NULL;
  for (unsigned i = 0; i < n; i++)
    {
      tree var = (*grp_vars)[i];
      tree g = grp_of (var);
      const char *gn = TREE_STRING_POINTER (TREE_PURPOSE (g));
      const char *wn = TREE_VALUE (g) ? TREE_STRING_POINTER (TREE_VALUE (g))
		       : "";
      char *spec = ACONCAT ((gn, ":", wn, NULL));
      vec<constructor_elt, va_gc> *e = NULL;
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 0),
			      fold_convert (TREE_TYPE (mp_field (et, 0)),
					    build_fold_addr_expr (var)));
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 1),
			      fold_convert (ptr_type_node,
					    build_string_literal
					      (strlen (spec) + 1, spec)));
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 2),
			      fold_convert (size_type_node,
					    DECL_SIZE_UNIT (var)));
      tree c = build_constructor (et, e);
      TREE_CONSTANT (c) = TREE_STATIC (c) = 1;
      CONSTRUCTOR_APPEND_ELT (v, size_int (i), c);
    }
  tree init = build_constructor (at, v);
  TREE_CONSTANT (init) = TREE_STATIC (init) = 1;
  tree d = build_decl (BUILTINS_LOCATION, VAR_DECL,
		       get_identifier ("__tzd_grpd_tab"), at);
  TREE_STATIC (d) = 1;
  TREE_PUBLIC (d) = 0;
  TREE_READONLY (d) = 1;
  DECL_ARTIFICIAL (d) = 1;
  TREE_USED (d) = 1;
  DECL_PRESERVE_P (d) = 1;
  SET_DECL_ALIGN (d, 64);
  DECL_USER_ALIGN (d) = 1;
  DECL_INITIAL (d) = init;
  set_decl_section_name (d, "tzd_grpd");
  rest_of_decl_compilation (d, 1, 0);
}

static void
add_stmt_tzd (tree t)
{
  if (error_p (t))
    return;
  append_to_statement_list_force (t, cur_stmts);
}

static tree
get_label (tree id, location_t loc)
{
  /* GNU local labels (__label__) of the enclosing blocks, innermost
     first.  */
  for (unsigned i = cur_fn->local_labels.length (); i >= 2; i -= 2)
    if (cur_fn->local_labels[i - 2] == id)
      return cur_fn->local_labels[i - 1];
  for (unsigned i = 0; i + 1 < cur_fn->labels.length (); i += 2)
    if (cur_fn->labels[i] == id)
      return cur_fn->labels[i + 1];
  tree l = build_decl (loc, LABEL_DECL, id, void_type_node);
  DECL_CONTEXT (l) = cur_fn->decl;
  cur_fn->labels.safe_push (id);
  cur_fn->labels.safe_push (l);
  return l;
}

/* ------------------------------------------------------------------ */
/* Inline asm (TK 2026-11-09: for now the same as C; nothing inside an
   asm can be checked).  Only the managed pointers at its edge are
   adapted: an input is passed as its raw address, and an output written
   by the asm gives the pointer a new address but keeps the bounds it had
   before (every later access is still checked against them, so an asm
   cannot forge bounds).  An asm that writes a pointer slot in memory
   ("=m") leaves the slot's bounds entry stale: the next load traps.  */

static ex parse_expr (void);
static ex parse_string (void);
/* Compound literal objects (any type) and asm temporaries, wiped by the
   innermost enclosing block (parse_compound) at exit.  */
static GTY(()) vec<tree, va_gc> *cl_wipes;

static tree aggregate_copy (tree l, tree r, location_t loc);
/* Queue an artificial local for the wipe at block exit -- unless it was
   made in an operand that is never evaluated (typeof (f (s)) in the
   kernel's alloc_hooks): such a temporary is never declared, and a wipe
   of it would reference an undeclared variable.  */
static void
cl_wipe_add (tree var)
{
  if (!unevaluated)
    vec_safe_push (cl_wipes, var);
}

static bool memory_ref_p (tree t);

/* A temporary object of struct TYPE for a lowered call (sv): the
   TARGET_EXPR (its first evaluation creates it) and the object.  INIT, if
   given, is copied in with its bounds entries.  Wiped at block exit.  */
static tree
sv_temp (tree type, tree init, tree *slot, location_t loc)
{
  tree var = build_decl (loc, VAR_DECL, NULL_TREE, type);
  DECL_ARTIFICIAL (var) = 1;
  DECL_CONTEXT (var) = current_function_decl;
  TREE_ADDRESSABLE (var) = 1;
  if (cur_fn)
    cl_wipe_add (var);
  tree in;
  if (init)
    in = fold_convert (void_type_node, aggregate_copy (var, init, loc));
  else
    in = build_constructor (type, NULL);
  tree te = build4 (TARGET_EXPR, type, var, in, NULL_TREE, NULL_TREE);
  TREE_SIDE_EFFECTS (te) = 1;
  SET_EXPR_LOCATION (te, loc);
  *slot = var;
  return te;
}

/* An artificial local of TYPE set to INIT before the asm (wiped at block
   exit like every local).  */
static tree
asm_temp (tree type, tree init, location_t loc)
{
  tree var = build_decl (loc, VAR_DECL, NULL_TREE, type);
  DECL_ARTIFICIAL (var) = 1;
  DECL_CONTEXT (var) = cur_fn->decl;
  cl_wipe_add (var);
  tree stmts = alloc_stmt_list ();
  if (init)
    append_to_statement_list_force (build2_loc (loc, INIT_EXPR, type, var,
						init), &stmts);
  tree te = build4 (TARGET_EXPR, type, var, stmts, NULL_TREE, NULL_TREE);
  TREE_SIDE_EFFECTS (te) = 1;
  SET_EXPR_LOCATION (te, loc);
  add_stmt_tzd (te);
  return var;
}

/* A string-literal attribute argument (section("..."), alias("...")).  */
static tree
attr_string_arg (void)
{
  ex e = parse_string ();
  return e.t;
}

static tree
asm_string (void)
{
  if (peek ().kind != TZD_STRING)
    {
      error_at (loc0 (), "expected string literal in %<asm%>");
      return error_mark_node;
    }
  ex e = parse_string ();
  tree st = e.t;
  TREE_TYPE (st) = TREE_TYPE (e.t);
  return st;
}

/* Is output operand IDX (named NAME, or NULL) of the asm being parsed
   tied to an input by a matching constraint ("0", "[name]")?  The tokens
   up to the asm's closing parenthesis are looked at ahead.  */
static bool
asm_output_matched (int idx, tree name)
{
  char num[16];
  snprintf (num, sizeof num, "%d", idx);
  int depth = 0;
  for (unsigned k = tpos; k < toks.length (); k++)
    {
      const tzd_token &t = toks[k];
      if (t.kind == TZD_EOF)
	break;
      if (t.kind == TZD_PUNCT && strcmp (t.text, "(") == 0)
	depth++;
      else if (t.kind == TZD_PUNCT && strcmp (t.text, ")") == 0)
	{
	  if (depth == 0)
	    break;
	  depth--;
	}
      else if (t.kind == TZD_STRING)
	{
	  const char *c = (const char *) t.str;
	  if (t.len == strlen (num) && memcmp (c, num, t.len) == 0)
	    return true;
	  if (name && t.len == (size_t) TREE_STRING_LENGTH (name) + 2
	      && c[0] == '[' && c[t.len - 1] == ']'
	      && memcmp (c + 1, TREE_STRING_POINTER (name), t.len - 2) == 0)
	    return true;
	}
    }
  return false;
}

/* Operand list "[name] "constraint" (expr), ...".  OUTPUTS: lvalues.
   POST collects the stores back into managed pointers.  */
static tree
asm_operands (bool outputs, tree *post, int *count)
{
  tree list = NULL_TREE;
  while (!is_p (":") && !is_p (")") && peek ().kind != TZD_EOF)
    {
      PROGRESS_GUARD;
      location_t loc = loc0 ();
      tree name = NULL_TREE;
      if (accept ("["))
	{
	  if (peek ().kind == TZD_IDENT)
	    {
	      name = build_string (strlen (peek ().text), peek ().text);
	      next ();
	    }
	  expect ("]");
	}
      tree cons = asm_string ();
      expect ("(");
      ex e = parse_expr ();
      expect (")");
      if (error_p (cons) || bad (e))
	{
	  if (!accept (","))
	    break;
	  continue;
	}
      const char *cs = TREE_STRING_POINTER (cons);
      /* Memory constraint letters, outside an operand name ("[o]").  */
      bool mem = false;
      for (const char *c = cs; *c; c++)
	if (*c == '[')
	  {
	    while (c[1] && c[1] != ']')
	      c++;
	  }
	else if (strchr ("moVQ", *c))
	  mem = true;
      tree op;
      if (outputs)
	{
	  if (!e.lv)
	    {
	      error_at (loc, "%<asm%> output is not an lvalue");
	      op = error_mark_node;
	    }
	  else if (TZD_MPTR_P (e.type) && !mem)
	    {
	      /* Register output into a managed pointer: the asm gets a raw
		 pointer temporary; afterwards the pointer takes its value as
		 the address and keeps its old bounds.  */
	      ex cur = rvalue (mk (stabilize_reference (e.t), e.type, true), loc);
	      parts (cur);
	      tree raw = mp_native (e.type);
	      tree base = asm_temp (ptr_type_node, cur.pb, loc);
	      tree size = asm_temp (size_type_node, cur.ps, loc);
	      tree r = asm_temp (raw, strchr (cs, '+') ? cur.pa : NULL_TREE, loc);
	      ex nv = mk_parts (e.type, r, base, size);
	      if (flag_tzd_kernel)
		{
		  /* Kernel mode: an output tied to its input ("+r", or "0"
		     as in OPTIMIZER_HIDE_VAR) moves the same pointer and
		     keeps its bounds, unless it had none.  Any other output
		     is a new address (this_cpu_read, rip_rel_ptr's physical
		     alias): it gets the bounds of the memory region it lies
		     in, never those of the variable's old value.  */
		  tree w = whole (nv);
		  tree fn = whole (from_native (r, e.type, loc));
		  if (strchr (cs, '+') || asm_output_matched (*count, name))
		    nv = mk (build3 (COND_EXPR, TREE_TYPE (w),
				     fold_build2_loc (loc, EQ_EXPR,
						      boolean_type_node, base,
						      null_pointer_node),
				     fn, w), e.type);
		  else
		    nv = mk (fn, e.type);
		}
	      tree l = stabilize_reference (e.t);
	      tree st;
	      if (TZD_MSLOT_P (TREE_TYPE (l)))
		st = slot_store (l, whole (nv), loc);
	      else
		st = build2_loc (loc, MODIFY_EXPR, TREE_TYPE (l), l, whole (nv));
	      append_to_statement_list_force (st, post);
	      op = r;
	    }
	  else if (TZD_MPTR_P (e.type) && TZD_MREC_P (TREE_TYPE (e.t)))
	    {
	      error_at (loc, "memory %<asm%> output to a managed pointer held "
			"in a register-like local is not supported");
	      op = error_mark_node;
	    }
	  else
	    {
	      op = e.t;
	      if (mem)
		mark_addressable_tree (op);
	    }
	}
      else
	{
	  if (mem && e.lv)
	    {
	      /* Kernel mode: a memory-only input ("m", no register
		 alternative under which the compiler would load the value
		 itself) hands the asm an address; what the asm does with
		 it is native code TzD cannot check either way, and forming
		 the operand reads nothing.  prefetchw(&current->mm->
		 mmap_lock) passes a pointer with no memory behind it, as it
		 may.  */
	      bool mem_only = true;
	      for (const char *c = cs; *c; c++)
		if (*c == '[')
		  {
		    while (c[1] && c[1] != ']')
		      c++;
		    if (c[1])
		      c++;
		  }
		else if (!strchr ("moVQ&%", *c))
		  mem_only = false;
	      if (flag_tzd_kernel && mem_only)
		e.t = drop_base_check (e.t);
	      op = e.t;
	      if (TZD_MREC_P (TREE_TYPE (op)))
		{
		  error_at (loc, "memory %<asm%> input of a managed pointer "
			    "held in a register-like local is not supported");
		  op = error_mark_node;
		}
	      else
		mark_addressable_tree (op);
	    }
	  else
	    {
	      /* "i" (func): the asm calls func itself.  */
	      if (strchr (cs, 'i') && e.t)
		{
		  tree fd = e.t;
		  STRIP_NOPS (fd);
		  if (TREE_CODE (fd) == ADDR_EXPR)
		    fd = TREE_OPERAND (fd, 0);
		  if (TREE_CODE (fd) == FUNCTION_DECL)
		    native_from_asm (fd, loc);
		}
	      ex v = rvalue (e, loc);
	      if (!bad (v) && TZD_MPTR_P (v.type))
		v = convert_to (v, build_pointer_type (TZD_MPTR_TARGET (v.type)),
				loc);
	      op = bad (v) ? error_mark_node : value (v);
	    }
	}
      list = chainon (list, build_tree_list (build_tree_list (name, cons),
					     op));
      (*count)++;
      if (!accept (","))
	break;
    }
  return list;
}

/* asm [volatile] [inline] [goto] ( template [: out [: in [: clobbers
   [: labels]]]] ), the keyword at the current token.  FILE_SCOPE: a
   top-level basic asm.  */
static void
parse_asm (bool file_scope)
{
  location_t loc = loc0 ();
  next ();
  bool vol = false, inl = false, is_goto = false;
  for (;;)
    {
      if (is_w ("volatile") || is_w ("__volatile__") || is_w ("__volatile"))
	vol = true;
      else if (is_w ("inline") || is_w ("__inline__") || is_w ("__inline"))
	inl = true;
      else if (is_w ("goto"))
	is_goto = true;
      else
	break;
      next ();
    }
  expect ("(");
  tree str = asm_string ();
  if (!error_p (str))
    native_from_asm_template (str, loc);
  if (file_scope)
    {
      expect (")");
      expect (";");
      if (!error_p (str))
	symtab->finalize_toplevel_asm (str);
      return;
    }
  tree outs = NULL_TREE, ins = NULL_TREE, clob = NULL_TREE, labels = NULL_TREE;
  tree post = alloc_stmt_list ();
  int nout = 0, nin = 0;
  bool basic = true;
  if (accept (":"))
    {
      basic = false;
      outs = asm_operands (true, &post, &nout);
      if (accept (":"))
	{
	  ins = asm_operands (false, &post, &nin);
	  if (accept (":"))
	    {
	      while (peek ().kind == TZD_STRING)
		{
		  tree c = asm_string ();
		  if (!error_p (c))
		    clob = chainon (clob, build_tree_list (NULL_TREE, c));
		  if (!accept (","))
		    break;
		}
	      if (accept (":"))
		while (peek ().kind == TZD_IDENT)
		  {
		    tree id = get_identifier (peek ().text);
		    tree l = get_label (id, loc0 ());
		    TREE_USED (l) = 1;
		    next ();
		    labels = chainon (labels,
				      build_tree_list
					(build_string (IDENTIFIER_LENGTH (id),
						       IDENTIFIER_POINTER (id)),
					 l));
		    if (!accept (","))
		      break;
		  }
	    }
	}
    }
  expect (")");
  if (!expect (";"))
    skip_to_semi ();
  if (error_p (str))
    return;
  for (tree t = outs; t; t = TREE_CHAIN (t))
    if (error_p (TREE_VALUE (t)))
      return;
  for (tree t = ins; t; t = TREE_CHAIN (t))
    if (error_p (TREE_VALUE (t)))
      return;
  if (labels && !is_goto)
    error_at (loc, "labels in %<asm%> without %<goto%>");
  if (!basic)
    str = resolve_asm_operand_names (str, outs, ins, labels);
  tree a = build5_loc (loc, ASM_EXPR, void_type_node, str, outs, ins, clob,
		       labels);
  ASM_INPUT_P (a) = basic;
  ASM_VOLATILE_P (a) = vol || basic || nout == 0 || is_goto;
  ASM_INLINE_P (a) = inl;
  TREE_SIDE_EFFECTS (a) = 1;
  add_stmt_tzd (a);
  add_stmt_tzd (post);
}

/* ------------------------------------------------------------------ */
/* Declarations: specifiers and declarators.  */

enum storage { ST_NONE, ST_TYPEDEF, ST_EXTERN, ST_STATIC, ST_AUTO,
	       ST_REGISTER };

struct dspec {
  enum storage st;
  bool inl;
  int quals;
  tree type;     /* struct/union/enum, typedef name, string */
  int n_void, n_char, n_short, n_int, n_long, n_float, n_double, n_bool;
  int n_int128;
  bool auto_type;   /* __auto_type: the type comes from the initializer */
  int n_signed, n_unsigned;
  bool any;
  location_t loc;
};

struct param_info {
  tree id;
  tree type;
  location_t loc;
};

struct fparams {
  vec<param_info> list;
  bool varargs;
  bool proto;
  bool knr;     /* K&R identifier list: types come from the declarations
		   before the body (default int) */
};

enum dop_kind { DOP_PTR, DOP_ARRAY, DOP_FUNC };

struct dop {
  enum dop_kind kind;
  int quals;
  tree size;        /* DOP_ARRAY: element count or NULL */
  fparams *fp;      /* DOP_FUNC */
};

struct declarator {
  tree id;
  location_t loc;
  vec<dop> ops;
  tree asm_name;
};

static bool typename_start_p (unsigned k = 0);
static void parse_declspecs (dspec &ds);
static tree finish_declspecs (dspec &ds);
static void parse_declarator (declarator &d, bool abstract_ok);
static tree apply_declarator (tree base, declarator &d, fparams **fn);
static ex parse_assign (void);
static ex parse_expr (void);
static ex parse_cond (void);
static tree parse_type_name (void);
static ex parse_string (void);
static tree knr_promoted (tree);
static ex member (ex e, tree id, location_t loc);
static void parse_compound (tree *body, bool fn_body, bool capture = false,
			    vec<tree> *spill = NULL);
/* Statement expressions: nesting depth of compound statements, and the
   depth / value type of the innermost ({ ... }) being parsed.  */
static int compound_depth;
static int stmtexpr_depth = -1;
static tree stmtexpr_type;
/* Result object of a statement expression whose value is a struct holding
   pointers: the last statement bounds-copies into it before the inner
   locals are wiped.  */
static tree stmtexpr_res;
/* Nesting of statement bodies (if/else/loop bodies) and its value at the
   start of the innermost statement expression: only that expression's
   own statements can give its value.  */
static int sub_stmt_depth;
static int stmtexpr_sub_depth;

static bool
typedef_name_p (const tzd_token &t)
{
  if (t.kind != TZD_IDENT)
    return false;
  tree d = tzd_lookup (get_identifier (t.text));
  return d && TREE_CODE (d) == TYPE_DECL;
}

/* The language type of an expression's type: a value record of a managed
   pointer is the pointer type itself (typeof, _Generic).  */
static tree
tzd_lang_type (tree t)
{
  if (t && TZD_MREC_P (t))
    return build_qualified_type (tzd_mptr_type (TZD_MPTR_TARGET (t)),
				 TYPE_QUALS (t));
  return t;
}

/* C type compatibility (C11 6.2.7), enough for _Generic and
   __builtin_types_compatible_p: top-level qualifiers of A and B must
   already be dropped by the caller where the language drops them.  */
static bool
tzd_types_compatible_p (tree a, tree b)
{
  a = tzd_lang_type (a);
  b = tzd_lang_type (b);
  if (TYPE_QUALS (a) != TYPE_QUALS (b))
    return false;
  a = TYPE_MAIN_VARIANT (a);
  b = TYPE_MAIN_VARIANT (b);
  if (a == b)
    return true;
  if (TREE_CODE (a) != TREE_CODE (b))
    return false;
  switch (TREE_CODE (a))
    {
    case POINTER_TYPE:
      if (TZD_MSLOT_P (a) != TZD_MSLOT_P (b))
	return false;
      return tzd_types_compatible_p (TREE_TYPE (a), TREE_TYPE (b));
    case ARRAY_TYPE:
      {
	if (!tzd_types_compatible_p (TREE_TYPE (a), TREE_TYPE (b)))
	  return false;
	tree sa = TYPE_SIZE (a), sb = TYPE_SIZE (b);
	return !sa || !sb || tree_int_cst_equal (sa, sb);
      }
    case FUNCTION_TYPE:
      {
	/* The calling convention is not part of the C type: a function
	   made native because an asm calls it (asm_prescan) is still a
	   void (struct pt_regs *) for the kernel's assert_function_type.  */
	a = native_fn_type (a);
	b = native_fn_type (b);
	if (!tzd_types_compatible_p (TREE_TYPE (a), TREE_TYPE (b)))
	  return false;
	tree x = TYPE_ARG_TYPES (a), y = TYPE_ARG_TYPES (b);
	if (!x || !y)
	  return true;
	for (; x && y; x = TREE_CHAIN (x), y = TREE_CHAIN (y))
	  if (!tzd_types_compatible_p (TREE_VALUE (x), TREE_VALUE (y)))
	    return false;
	return x == y;
      }
    default:
      return false;
    }
}

static const char *const type_words[] = {
  "void", "char", "short", "int", "long", "float", "double", "signed",
  "unsigned", "_Bool", "struct", "union", "enum", "const", "volatile",
  "restrict", "__const", "__const__", "__volatile__", "__volatile",
  "__restrict", "__restrict__", "__signed__", "__signed", "string", "bool",
  "__int128", "__int128__", "typeof", "__typeof__", "__typeof",
  "typeof_unqual", "__typeof_unqual__", "__auto_type", 0
};

static const char *const decl_words[] = {
  "typedef", "extern", "static", "auto", "register", "inline", "__inline",
  "__inline__", "_Noreturn", "__attribute__", "__attribute",
  "__extension__", "_Static_assert", 0
};

static bool
in_words (const char *const *list, const tzd_token &t)
{
  if (t.kind != TZD_KEYWORD && t.kind != TZD_IDENT)
    return false;
  /* string and bool are type words only as TzD (.d) keywords; in Managed C
     they stay ordinary identifiers (a variable or typedef named bool).  */
  if (t.kind == TZD_IDENT
      && (strcmp (t.text, "string") == 0 || strcmp (t.text, "bool") == 0))
    return false;
  for (; *list; list++)
    if (strcmp (*list, t.text) == 0)
      return true;
  return false;
}

/* Can a type-name start at token K?  */
static bool
typename_start_p (unsigned k)
{
  const tzd_token &t = peek (k);
  return in_words (type_words, t) || typedef_name_p (t)
	 /* _Atomic starts a type under #pragma tzd atomics (task #61), so a
	    cast or type-name like (_Atomic int *) is recognised.  */
	 || (tzd_pragma_atomics
	     && (t.kind == TZD_KEYWORD || t.kind == TZD_IDENT)
	     && strcmp (t.text, "_Atomic") == 0);
}

static bool
decl_start_p (void)
{
  /* __extension__ prefixes either a declaration or an expression statement
     (stdatomic.h expands atomic_store_explicit(...) to __extension__({...}),
     used as a statement in OpenSSL's tsan_assist.h); decide by what follows
     it, not by __extension__ itself.  */
  if (is_w ("__extension__"))
    {
      unsigned k = 1;
      while (is_w ("__extension__", k))
	k++;
      return typename_start_p (k) || in_words (decl_words, peek (k));
    }
  return typename_start_p (0) || in_words (decl_words, peek ());
}

/* "__attribute__ ((...)) ;" at the current token: a null statement with
   attributes (fallthrough).  */
static bool
attr_stmt_p (void)
{
  if (!is_w ("__attribute__") && !is_w ("__attribute"))
    return false;
  unsigned k = tpos + 1, depth = 0;
  for (; k < toks.length (); k++)
    {
      const tzd_token &t = toks[k];
      if (t.kind == TZD_PUNCT && strcmp (t.text, "(") == 0)
	depth++;
      else if (t.kind == TZD_PUNCT && strcmp (t.text, ")") == 0)
	{
	  if (--depth == 0)
	    break;
	}
      else if (t.kind == TZD_EOF)
	return false;
    }
  return k + 1 < toks.length () && toks[k + 1].kind == TZD_PUNCT
	 && strcmp (toks[k + 1].text, ";") == 0;
}

static int
parse_quals (void)
{
  int q = 0;
  for (;;)
    {
      if (is_w ("const") || is_w ("__const") || is_w ("__const__"))
	q |= TYPE_QUAL_CONST;
      else if (is_w ("volatile") || is_w ("__volatile")
	       || is_w ("__volatile__"))
	q |= TYPE_QUAL_VOLATILE;
      else if (is_w ("restrict") || is_w ("__restrict")
	       || is_w ("__restrict__"))
	;   /* restrict has no meaning for managed pointers yet */
      else if (skip_gnu_noise ())
	continue;
      else
	return q;
      next ();
    }
}

static tree parse_struct (bool is_union);
static tree parse_enum (void);

static void
parse_declspecs (dspec &ds)
{
  memset (&ds, 0, sizeof ds);
  ds.loc = loc0 ();
  for (;;)
    {
      const tzd_token &t = peek ();
      if (t.kind != TZD_KEYWORD && t.kind != TZD_IDENT)
	return;
      const char *w = t.text;
      bool have_type = ds.type || ds.n_void || ds.n_char || ds.n_short
		       || ds.n_int || ds.n_long || ds.n_float || ds.n_double
		       || ds.n_bool || ds.n_signed || ds.n_unsigned
		       || ds.n_int128;

      if (skip_gnu_noise ())
	continue;
      if (strcmp (w, "typedef") == 0) ds.st = ST_TYPEDEF;
      else if (strcmp (w, "extern") == 0) ds.st = ST_EXTERN;
      else if (strcmp (w, "static") == 0) ds.st = ST_STATIC;
      else if (strcmp (w, "auto") == 0) ds.st = ST_AUTO;
      else if (strcmp (w, "register") == 0) ds.st = ST_REGISTER;
      else if (strcmp (w, "inline") == 0 || strcmp (w, "__inline") == 0
	       || strcmp (w, "__inline__") == 0)
	ds.inl = true;
      else if (strcmp (w, "_Noreturn") == 0)
	{
	  /* As __attribute__((noreturn)) on the declaration.  */
	  if (!attr_decl_list)
	    attr_decl_loc = loc0 ();
	  attr_decl_list = tree_cons (get_identifier ("noreturn"), NULL_TREE,
				      attr_decl_list);
	}
      else if (strcmp (w, "const") == 0 || strcmp (w, "__const") == 0
	       || strcmp (w, "__const__") == 0)
	ds.quals |= TYPE_QUAL_CONST;
      else if (strcmp (w, "volatile") == 0 || strcmp (w, "__volatile") == 0
	       || strcmp (w, "__volatile__") == 0)
	ds.quals |= TYPE_QUAL_VOLATILE;
      else if (strcmp (w, "restrict") == 0 || strcmp (w, "__restrict") == 0
	       || strcmp (w, "__restrict__") == 0)
	;
      else if (strcmp (w, "void") == 0) ds.n_void++;
      else if (strcmp (w, "char") == 0) ds.n_char++;
      else if (strcmp (w, "short") == 0) ds.n_short++;
      else if (strcmp (w, "int") == 0) ds.n_int++;
      else if (strcmp (w, "long") == 0) ds.n_long++;
      else if (strcmp (w, "float") == 0) ds.n_float++;
      else if (strcmp (w, "double") == 0) ds.n_double++;
      else if (strcmp (w, "__int128") == 0 || strcmp (w, "__int128__") == 0)
	ds.n_int128++;
      else if (strcmp (w, "__auto_type") == 0)
	ds.auto_type = true;
      else if (strcmp (w, "_Bool") == 0
	       || (t.kind == TZD_KEYWORD && strcmp (w, "bool") == 0))
	ds.n_bool++;
      else if (strcmp (w, "signed") == 0 || strcmp (w, "__signed__") == 0
	       || strcmp (w, "__signed") == 0)
	ds.n_signed++;
      else if (strcmp (w, "unsigned") == 0) ds.n_unsigned++;
      else if (t.kind == TZD_KEYWORD && strcmp (w, "string") == 0)
	{
	  /* TzD (.d): string is the managed pointer to char (task #20).  */
	  ds.type = tzd_mptr_type (char_type_node);
	}
      else if (strcmp (w, "struct") == 0 || strcmp (w, "union") == 0)
	{
	  next ();
	  ds.type = parse_struct (w[0] == 'u');
	  ds.any = true;
	  continue;
	}
      else if (strcmp (w, "enum") == 0)
	{
	  next ();
	  ds.type = parse_enum ();
	  ds.any = true;
	  continue;
	}
      else if (strcmp (w, "typeof") == 0 || strcmp (w, "__typeof__") == 0
	       || strcmp (w, "__typeof") == 0
	       || strcmp (w, "typeof_unqual") == 0
	       || strcmp (w, "__typeof_unqual__") == 0)
	{
	  /* typeof (type-name) or typeof (expression); the expression is
	     not evaluated.  */
	  bool unqual = strstr (w, "unqual") != NULL;
	  location_t tl = t.loc;
	  next ();
	  expect ("(");
	  tree ty;
	  if (typename_start_p (0))
	    ty = parse_type_name ();
	  else
	    {
	      unevaluated++;
	      ex e = parse_expr ();
	      unevaluated--;
	      ty = bad (e) ? error_mark_node : tzd_lang_type (e.type);
	    }
	  expect (")");
	  if (!error_p (ty) && unqual)
	    ty = TYPE_MAIN_VARIANT (ty);
	  if (error_p (ty))
	    error_at (tl, "invalid operand of %<typeof%>");
	  ds.type = ty;
	  ds.any = true;
	  continue;
	}
      else if (strcmp (w, "_Atomic") == 0 && tzd_pragma_atomics
	       && !is_p ("(", 1))
	{
	  /* C11 _Atomic qualifier under #pragma tzd atomics (task #61): for
	     scalar lock-free types the representation is unchanged and the
	     atomic operations lower to __atomic builtins, so TzD treats
	     `_Atomic T` as `T` (atomicity is a run-time property, not a
	     bounds concern).  */
	  next ();
	  continue;
	}
      else if (strcmp (w, "_Atomic") == 0 && tzd_pragma_atomics)
	{
	  /* The `_Atomic (type-name)` specifier form yields the type.  */
	  next ();
	  expect ("(");
	  tree ty = parse_type_name ();
	  expect (")");
	  ds.type = error_p (ty) ? error_mark_node : ty;
	  ds.any = true;
	  continue;
	}
      else if (!have_type && typedef_name_p (t))
	ds.type = TREE_TYPE (tzd_lookup (get_identifier (w)));
      else
	return;
      ds.any = true;
      next ();
    }
}

static tree finish_declspecs_1 (dspec &ds);

/* The declaration specifiers' type, with a mode attribute given among
   them.  */
static tree
finish_declspecs (dspec &ds)
{
  return apply_attr_mode (finish_declspecs_1 (ds));
}

static tree
finish_declspecs_1 (dspec &ds)
{
  tree t;
  if (ds.auto_type)
    t = integer_type_node;   /* replaced by the initializer's type */
  else if (ds.type)
    t = ds.type;
  else if (ds.n_void)
    t = void_type_node;
  else if (ds.n_bool)
    t = boolean_type_node;
  else if (ds.n_char)
    t = ds.n_unsigned ? unsigned_char_type_node
	: ds.n_signed ? signed_char_type_node : char_type_node;
  else if (ds.n_int128)
    t = ds.n_unsigned ? unsigned_intTI_type_node : intTI_type_node;
  else if (ds.n_float)
    t = float_type_node;
  else if (ds.n_double)
    t = ds.n_long ? long_double_type_node : double_type_node;
  else if (ds.n_short)
    t = ds.n_unsigned ? short_unsigned_type_node : short_integer_type_node;
  else if (ds.n_long >= 2)
    t = ds.n_unsigned ? long_long_unsigned_type_node
	: long_long_integer_type_node;
  else if (ds.n_long == 1)
    t = ds.n_unsigned ? long_unsigned_type_node : long_integer_type_node;
  else
    {
      if (!ds.n_int && !ds.n_signed && !ds.n_unsigned)
	{
	  if (!ds.any)
	    error_at (ds.loc, "expected declaration specifiers");
	  else if (ds.st != ST_TYPEDEF)
	    warning_at (ds.loc, 0, "type defaults to %<int%>");
	}
      t = ds.n_unsigned ? unsigned_type_node : integer_type_node;
    }
  if (ds.quals)
    t = build_qualified_type (t, TYPE_QUALS (t) | ds.quals);
  return t;
}

/* Integer constant expression.  */
static tree
parse_const_int (void)
{
  location_t loc = loc0 ();
  ex e = parse_cond ();
  e = rvalue (e, loc);
  if (bad (e))
    return NULL_TREE;
  tree v = fold (e.t);
  if (TREE_CODE (v) != INTEGER_CST)
    {
      error_at (loc, "expression is not an integer constant");
      return NULL_TREE;
    }
  return v;
}

/* Merge two sets of pending layout attributes.  */
static attr_layout
merge_attr_layout (attr_layout a, attr_layout b)
{
  if (b.align > a.align)
    a.align = b.align;
  if (b.packed)
    a.packed = true;
  if (b.transparent)
    a.transparent = true;
  if (b.may_alias)
    a.may_alias = true;
  if (b.align || b.packed || b.transparent || b.may_alias)
    a.loc = b.loc;
  return a;
}

/* aligned/packed on a struct member, as in the C front end: aligned can
   only raise the member's alignment (unless packed), packed drops it to
   a byte.  */
static void
apply_field_layout (tree f, attr_layout a)
{
  if (a.packed || TYPE_PACKED (DECL_CONTEXT (f)))
    {
      if (TYPE_ALIGN (TREE_TYPE (f)) > BITS_PER_UNIT)
	DECL_PACKED (f) = 1;
    }
  if (a.align)
    {
      SET_DECL_ALIGN (f, MAX (DECL_ALIGN (f), a.align * BITS_PER_UNIT));
      DECL_USER_ALIGN (f) = 1;
    }
}

/* Does an object of TYPE hold a managed pointer slot anywhere?  */
static bool
type_has_mslot (tree type)
{
  type = TYPE_MAIN_VARIANT (type);
  if (TZD_MSLOT_P (type))
    return true;
  if (TREE_CODE (type) == ARRAY_TYPE)
    return type_has_mslot (TREE_TYPE (type));
  if (RECORD_OR_UNION_TYPE_P (type))
    for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
      if (TREE_CODE (f) == FIELD_DECL && type_has_mslot (TREE_TYPE (f)))
	return true;
  return false;
}

/* After layout: the bounds table keys 8-byte aligned pointer slots, so a
   slot that packed (or a small alignment) put at an offset that is not a
   multiple of 8 is a compile error -- a pointer member itself or one
   nested in a member (task #42, TK 2026-11-13: stop at compile time and
   at run time).  The layout stays GCC's: a packed type keeps alignment 1
   (raising it would move it inside other structs).  An array of a type
   that holds a slot needs an element size that is a multiple of 8
   (array_slot_check), and a variable of such a type is placed 8-byte
   aligned (var_slot_align).  Run time still traps on a misaligned slot
   (shadow.c sh_index_check), e.g. a packed object reached through a cast
   or embedded where the compiler cannot see it.
   For packed structs/unions, misaligned pointer fields are demoted to
   plain pointers (task #75): UAPI structs such as v4l2_ext_control are
   __packed with pointer-union payloads and cannot be changed; demoting
   prevents kc2d from seeing misaligned slot addresses at tzd_shinit
   time.  */

/* Strip managed pointer slot types within TYPE down to plain pointers.
   Called for fields of a packed struct that land at a misaligned offset.  */
static void
demote_mslots_in_type (tree type)
{
  for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      tree ft = TREE_TYPE (f);
      if (TZD_MSLOT_P (ft))
	TREE_TYPE (f)
	  = build_qualified_type (build_pointer_type (TZD_MPTR_TARGET (ft)),
				  TYPE_QUALS (ft));
      else if (TREE_CODE (ft) == RECORD_TYPE || TREE_CODE (ft) == UNION_TYPE)
	demote_mslots_in_type (ft);
    }
}

static void
check_packed_slots (tree type, location_t loc)
{
  for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL || !DECL_FIELD_OFFSET (f))
	continue;
      if (!type_has_mslot (TREE_TYPE (f)))
	continue;
      tree bp = bit_position (f);
      if (!tree_fits_uhwi_p (bp) || tree_to_uhwi (bp) % 64 != 0)
	{
	  if (!TYPE_PACKED (type))
	    {
	      location_t eloc = DECL_SOURCE_LOCATION (f) != UNKNOWN_LOCATION
			        ? DECL_SOURCE_LOCATION (f) : loc;
	      /* DECL_NAME is NULL for anonymous struct/union members; use a
		 different message to avoid crashing in the diagnostic printer. */
	      if (DECL_NAME (f))
		error_at (eloc,
			  "member %qE holds a pointer at an offset that is not a "
			  "multiple of 8 bytes (packed); the bounds table keys "
			  "8-byte aligned pointer slots", DECL_NAME (f));
	      else
		error_at (eloc,
			  "anonymous member holds a pointer at an offset that is "
			  "not a multiple of 8 bytes (packed); the bounds table "
			  "keys 8-byte aligned pointer slots");
	      return;
	    }
	  /* Packed: demote managed slots in this misaligned field to plain
	     pointers; kc2d cannot track misaligned slot addresses.  */
	  tree ft = TREE_TYPE (f);
	  if (TZD_MSLOT_P (ft))
	    TREE_TYPE (f)
	      = build_qualified_type (build_pointer_type (TZD_MPTR_TARGET (ft)),
				      TYPE_QUALS (ft));
	  else
	    demote_mslots_in_type (ft);
	}
    }
}

/* An array whose elements hold a pointer slot: every element after the
   first must start 8-byte aligned.  */
static bool
array_slot_check (tree elt, location_t loc)
{
  if (!type_has_mslot (elt))
    return true;
  tree sz = TYPE_SIZE (elt);
  if (sz && tree_fits_uhwi_p (sz) && tree_to_uhwi (sz) % 64 != 0)
    {
      error_at (loc, "array of a type that holds a pointer and whose size "
		"(packed) is not a multiple of 8 bytes; the bounds table "
		"keys 8-byte aligned pointer slots");
      return false;
    }
  return true;
}

/* A variable that holds a pointer slot is placed 8-byte aligned (a
   packed type has alignment 1; its layout is unchanged).  */
static void
var_slot_align (tree var)
{
  if (VAR_P (var) && DECL_ALIGN (var) < 64 && type_has_mslot (TREE_TYPE (var)))
    {
      SET_DECL_ALIGN (var, 64);
      DECL_USER_ALIGN (var) = 1;
    }
}

/* _Static_assert ( constant-expression [, string-literal] ) ;  the
   keyword at the current token.  */
static void
parse_static_assert (void)
{
  location_t loc = loc0 ();
  next ();
  expect ("(");
  tree v = parse_const_int ();
  const char *msg = NULL;
  if (accept (",") && peek ().kind == TZD_STRING)
    {
      tree m = attr_string_arg ();
      msg = TREE_STRING_POINTER (m);
    }
  expect (")");
  expect (";");
  if (v && integer_zerop (v))
    {
      if (msg)
	error_at (loc, "static assertion failed: %qs", msg);
      else
	error_at (loc, "static assertion failed");
    }
}

static tree
parse_struct (bool is_union)
{
  location_t loc = loc0 ();
  /* Attributes pending from the enclosing declaration are not ours.  */
  attr_layout outer_lay = take_attr_layout ();
  tree outer_pass = attr_decl_list;
  attr_decl_list = NULL_TREE;
  skip_gnu_noise (false);
  attr_layout type_lay = take_attr_layout ();
  tree tag = NULL_TREE;
  if (peek ().kind == TZD_IDENT)
    {
      tag = get_identifier (peek ().text);
      next ();
    }
  enum tree_code code = is_union ? UNION_TYPE : RECORD_TYPE;
  tree type = NULL_TREE;

  if (!is_p ("{"))
    {
      /* struct x __attribute__((...)) without a body: the attributes stay
	 with the declaration (as in GCC, for a variable or typedef).  */
      attr_lay = merge_attr_layout (outer_lay, type_lay);
      attr_decl_list = chainon (outer_pass, attr_decl_list);
      if (tag == NULL_TREE)
	{
	  error_at (loc, "expected %<{%>");
	  return error_mark_node;
	}
      /* "struct x;" declares a new tag in this scope.  */
      bool fwd = is_p (";");
      type = tzd_lookup_tag (tag, fwd);
      if (type == NULL_TREE)
	{
	  type = make_node (code);
	  TYPE_NAME (type) = tag;
	  tzd_push_tag (tag, type);
	}
      return type;
    }

  if (tag)
    {
      type = tzd_lookup_tag (tag, true);
      if (type && COMPLETE_TYPE_P (type))
	{
	  error_at (loc, "redefinition of %qE", tag);
	  type = NULL_TREE;
	}
    }
  if (type == NULL_TREE)
    {
      type = make_node (code);
      TYPE_NAME (type) = tag;
      if (tag)
	tzd_push_tag (tag, type);
    }

  next ();   /* { */
  tree fields = NULL_TREE, *tail = &fields;
  auto_vec<std::pair<tree, attr_layout> > field_lays;
  while (!is_p ("}") && peek ().kind != TZD_EOF)
    {
      PROGRESS_GUARD;
      if (is_w ("_Static_assert"))
	{
	  parse_static_assert ();
	  continue;
	}
      if (accept (";"))
	continue;   /* empty member declaration (GNU C) */
      dspec ds;
      take_attr_layout ();
      parse_declspecs (ds);
      tree base = finish_declspecs (ds);
      attr_layout spec_lay = take_attr_layout ();
      if (is_p (";"))
	{
	  /* Anonymous struct/union member (C11): an unnamed field whose
	     members are found through it (see find_field).  */
	  if (RECORD_OR_UNION_TYPE_P (base) && !TYPE_NAME (base)
	      && !TZD_MPTR_P (base))
	    {
	      tree f = build_decl (loc0 (), FIELD_DECL, NULL_TREE, base);
	      DECL_CONTEXT (f) = type;
	      field_lays.safe_push (std::make_pair (f, spec_lay));
	      *tail = f;
	      tail = &DECL_CHAIN (f);
	    }
	  next ();
	  continue;
	}
      for (;;)
	{
	  declarator d;
	  memset (&d, 0, sizeof d);
	  tree ft = base;
	  if (!is_p (":"))
	    {
	      parse_declarator (d, false);
	      fparams *fp;
	      ft = apply_declarator (base, d, &fp);
	    }
	  else
	    d.loc = loc0 ();
	  tree width = NULL_TREE;
	  if (accept (":"))
	    {
	      width = parse_const_int ();
	      if (width && (!INTEGRAL_TYPE_P (ft) || TZD_MPTR_P (ft)))
		{
		  error_at (d.loc, "bit-field has invalid type");
		  width = NULL_TREE;
		}
	      else if (width && (tree_int_cst_sgn (width) < 0
				 || compare_tree_int (width,
						      TYPE_PRECISION (ft)) > 0))
		{
		  error_at (d.loc, "bit-field width out of range");
		  width = NULL_TREE;
		}
	    }
	  skip_gnu_noise ();
	  ft = apply_attr_mode (ft);
	  if (!error_p (ft) && (d.id || width))
	    {
	      if (TREE_CODE (ft) == FUNCTION_TYPE)
		error_at (d.loc, "field %qE declared as a function", d.id);
	      else
		{
		  if (TREE_CODE (ft) == ARRAY_TYPE && !TYPE_DOMAIN (ft))
		    /* Flexible array member: domain [0, unknown] as in GCC's
		       C front end (varasm sizes a static initializer of it,
		       mm_struct's cpu_bitmap = CPU_BITS_NONE, from it).  */
		    ft = build_qualified_type
			   (build_array_type (TREE_TYPE (ft),
					      build_range_type (sizetype,
								size_zero_node,
								NULL_TREE)),
			    TYPE_QUALS (ft));
		  tree f = build_decl (d.loc, FIELD_DECL, d.id, ft);
		  DECL_CONTEXT (f) = type;
		  if (attr_decl_list && TREE_CODE (ft) == POINTER_TYPE
		      && TREE_CODE (TREE_TYPE (ft)) == FUNCTION_TYPE)
		    {
		      /* A calling-convention attribute on a function pointer
			 member (efi.h's __efiapi = ms_abi): GCC's handler
			 puts it on the pointed-to function type, as in C.  */
		      decl_attributes (&f, attr_decl_list, 0);
		      attr_decl_list = NULL_TREE;
		    }
		  field_lays.safe_push
		    (std::make_pair (f, merge_attr_layout
					  (spec_lay, take_attr_layout ())));
		  if (width)
		    {
		      /* Same as the C front end: DECL_SIZE = width; after
			 layout the type becomes a WIDTH-bit integer.  */
		      DECL_SIZE (f) = bitsize_int (tree_to_uhwi (width));
		      DECL_BIT_FIELD (f) = 1;
		      /* As the C front end (grokdeclarator): a bit-field has
			 no address.  Without it the optimizers (ivopts) took
			 &tab[i].k and stored a whole byte through it.  */
		      DECL_NONADDRESSABLE_P (f) = 1;
		      if (integer_zerop (width))
			DECL_NAME (f) = NULL_TREE;
		    }
		  *tail = f;
		  tail = &DECL_CHAIN (f);
		}
	    }
	  d.ops.release ();
	  if (!accept (","))
	    break;
	}
      expect (";");
    }
  expect ("}");
  skip_gnu_noise (false);
  type_lay = merge_attr_layout (type_lay, take_attr_layout ());
  attr_lay = outer_lay;
  /* `unused` on a member only silences a warning: drop it.  */
  for (tree *pa = &attr_decl_list; *pa;)
    if (is_attribute_p ("unused", get_attribute_name (*pa))
	|| is_attribute_p ("used", get_attribute_name (*pa)))
      *pa = TREE_CHAIN (*pa);
    else
      pa = &TREE_CHAIN (*pa);
  if (attr_decl_list)
    error_at (attr_decl_loc, "attribute %qE does not apply to a struct or "
	      "its members (task #26)", TREE_PURPOSE (attr_decl_list));
  attr_decl_list = outer_pass;
  TYPE_FIELDS (type) = fields;
  if (type_lay.packed)
    TYPE_PACKED (type) = 1;
  if (type_lay.transparent)
    {
      if (is_union)
	TYPE_TRANSPARENT_AGGR (type) = 1;
      else
	error_at (type_lay.loc, "attribute %<transparent_union%> applies only "
		  "to unions");
    }
  if (type_lay.align)
    {
      SET_TYPE_ALIGN (type, MAX (TYPE_ALIGN (type),
				 type_lay.align * BITS_PER_UNIT));
      TYPE_USER_ALIGN (type) = 1;
    }
  for (unsigned i = 0; i < field_lays.length (); i++)
    apply_field_layout (field_lays[i].first, field_lays[i].second);
  /* #pragma tzd align_member TYPE.FIELD N (task #70): TYPE is the tag, or
     for an untagged body the name declared right after it (the typedef
     name of `typedef struct { ... } NAME;`).  */
  {
    const char *tn = type_name_str (type);
    if (!tn && peek ().kind == TZD_IDENT)
      tn = peek ().text;
    if (tn)
      for (tree f = fields; f; f = DECL_CHAIN (f))
	if (TREE_CODE (f) == FIELD_DECL && DECL_NAME (f))
	  if (int nb = tzd_align_member (tn, IDENTIFIER_POINTER (DECL_NAME (f))))
	    {
	      SET_DECL_ALIGN (f, MAX (DECL_ALIGN (f), nb * BITS_PER_UNIT));
	      DECL_USER_ALIGN (f) = 1;
	    }
  }
  layout_type (type);
  check_packed_slots (type, loc);
  for (tree f = fields; f; f = DECL_CHAIN (f))
    if (DECL_BIT_FIELD_TYPE (f)
	&& TREE_CODE (TREE_TYPE (f)) == INTEGER_TYPE)
      {
	unsigned HOST_WIDE_INT w = tree_to_uhwi (DECL_SIZE (f));
	if (w && w != TYPE_PRECISION (TREE_TYPE (f)))
	  {
	    TREE_TYPE (f)
	      = build_nonstandard_integer_type (w, TYPE_UNSIGNED (TREE_TYPE (f)));
	    SET_DECL_MODE (f, TYPE_MODE (TREE_TYPE (f)));
	  }
      }
  /* Complete the variants created while it was incomplete.  */
  for (tree v = TYPE_MAIN_VARIANT (type); v; v = TYPE_NEXT_VARIANT (v))
    if (v != type)
      {
	TYPE_FIELDS (v) = fields;
	TYPE_SIZE (v) = TYPE_SIZE (type);
	TYPE_SIZE_UNIT (v) = TYPE_SIZE_UNIT (type);
	SET_TYPE_ALIGN (v, TYPE_ALIGN (type));
	SET_TYPE_MODE (v, TYPE_MODE (type));
      }
  if (TYPE_STUB_DECL (type) == NULL_TREE)
    {
      tree stub = build_decl (loc, TYPE_DECL, NULL_TREE, type);
      DECL_ARTIFICIAL (stub) = 1;
      TYPE_STUB_DECL (type) = stub;
    }
  rest_of_type_compilation (type, global_bindings_p ());
  return type;
}

static tree
parse_enum (void)
{
  location_t loc = loc0 ();
  skip_gnu_noise (false);
  tree tag = NULL_TREE;
  if (peek ().kind == TZD_IDENT)
    {
      tag = get_identifier (peek ().text);
      next ();
    }
  /* The enumerated type is an integer type chosen as GCC does (task:
     decide whether TzD enums are distinct types): unsigned int when no
     value is negative, int otherwise, and a 64-bit type when a value
     does not fit in 32 bits.  Enumerators have type int when their value
     fits in int, else the enumerated type.  A value is never truncated.  */
  tree type = unsigned_type_node;
  if (tag && !is_p ("{"))
    {
      if (!tzd_lookup_tag (tag, false))
	tzd_push_tag (tag, type);
      return tzd_lookup_tag (tag, false);
    }
  if (!expect ("{"))
    return error_mark_node;
  if (tag)
    tzd_push_tag (tag, type);
  widest_int val = 0, vmin = 0, vmax = 0;
  bool any = false;
  auto_vec<tree> consts;
  while (!is_p ("}") && peek ().kind != TZD_EOF)
    {
      if (peek ().kind != TZD_IDENT)
	{
	  error_at (loc0 (), "expected identifier in enumerator list");
	  break;
	}
      tree id = get_identifier (peek ().text);
      location_t eloc = loc0 ();
      next ();
      skip_gnu_noise (false);
      if (accept ("="))
	{
	  tree v = parse_const_int ();
	  if (v)
	    val = wi::to_widest (v);
	}
      if (!any || wi::lts_p (val, vmin))
	vmin = val;
      if (!any || wi::gts_p (val, vmax))
	vmax = val;
      any = true;
      tree ct;
      if (wi::fits_to_tree_p (val, integer_type_node))
	ct = integer_type_node;
      else if (wi::fits_to_tree_p (val, long_integer_type_node))
	ct = long_integer_type_node;
      else if (wi::fits_to_tree_p (val, long_unsigned_type_node))
	ct = long_unsigned_type_node;
      else
	{
	  error_at (eloc, "enumerator value does not fit in 64 bits");
	  ct = integer_type_node;
	  val = 0;
	}
      tree c = build_decl (eloc, CONST_DECL, id, ct);
      DECL_INITIAL (c) = wide_int_to_tree (ct, val);
      pushdecl (c);
      consts.safe_push (c);
      val = val + 1;
      if (!accept (","))
	break;
    }
  expect ("}");
  /* enum {...} __attribute__((packed)): the smallest integer type that
     holds every value (GCC).  Attributes pending from the enclosing
     declaration are kept for it.  */
  attr_layout outer_lay = take_attr_layout ();
  skip_gnu_noise (false);
  attr_layout enum_lay = take_attr_layout ();
  attr_lay = outer_lay;
  if (enum_lay.align)
    error_at (enum_lay.loc, "attribute %<aligned%> on an enumerated type is "
	      "not supported (task #26)");
  /* The type that holds every value.  */
  bool nonneg = !any || wi::ges_p (vmin, 0);
  if (enum_lay.packed)
    {
      static tree *const cand[][2] = {
	{ &unsigned_char_type_node, &signed_char_type_node },
	{ &short_unsigned_type_node, &short_integer_type_node },
	{ &unsigned_type_node, &integer_type_node },
	{ &long_unsigned_type_node, &long_integer_type_node } };
      type = long_integer_type_node;
      for (unsigned k = 0; k < 4; k++)
	{
	  tree t = *cand[k][nonneg ? 0 : 1];
	  if (wi::fits_to_tree_p (vmin, t) && wi::fits_to_tree_p (vmax, t))
	    {
	      type = t;
	      break;
	    }
	}
    }
  else if (nonneg)
    type = wi::fits_to_tree_p (vmax, unsigned_type_node)
	   ? unsigned_type_node : long_unsigned_type_node;
  else
    type = (wi::fits_to_tree_p (vmin, integer_type_node)
	    && wi::fits_to_tree_p (vmax, integer_type_node))
	   ? integer_type_node : long_integer_type_node;
  if (any && wi::lts_p (vmin, 0)
      && !wi::fits_to_tree_p (vmax, long_integer_type_node))
    error_at (loc, "enumerator values do not fit in one integer type");
  for (unsigned k = 0; k < consts.length (); k++)
    {
      tree c = consts[k];
      if (TREE_TYPE (c) != integer_type_node)
	{
	  TREE_TYPE (c) = type;
	  DECL_INITIAL (c) = fold_convert (type, DECL_INITIAL (c));
	}
    }
  if (tag)
    tzd_push_tag (tag, type);
  return type;
}

/* Does '(' at token K start a nested declarator (rather than a parameter
   list)?  */
static bool
grouping_paren_p (unsigned k)
{
  /* ( __attribute__ ((...)) * name ): attributes may open a grouped
     declarator (efi.h's efi_status_t (__efiapi *handler)(...)); look at
     what follows them.  */
  while (peek (k + 1).kind == TZD_IDENT
	 && (strcmp (peek (k + 1).text, "__attribute__") == 0
	     || strcmp (peek (k + 1).text, "__attribute") == 0)
	 && peek (k + 2).kind == TZD_PUNCT && strcmp (peek (k + 2).text, "(") == 0)
    {
      unsigned j = k + 2;
      int depth = 0;
      do
	{
	  const tzd_token &u = peek (j);
	  if (u.kind == TZD_EOF)
	    return false;
	  if (u.kind == TZD_PUNCT && strcmp (u.text, "(") == 0)
	    depth++;
	  else if (u.kind == TZD_PUNCT && strcmp (u.text, ")") == 0)
	    depth--;
	  j++;
	}
      while (depth > 0);
      k = j - 1;
    }
  const tzd_token &t = peek (k + 1);
  if (t.kind == TZD_PUNCT)
    return strcmp (t.text, "*") == 0 || strcmp (t.text, "(") == 0;
  if (t.kind == TZD_IDENT && !typedef_name_p (t)
      && strcmp (t.text, "__attribute__") != 0)
    return true;
  return false;
}

static int param_depth;   /* inside a parameter list */

static fparams *parse_params_1 (void);

static fparams *
parse_params (void)
{
  param_depth++;
  fparams *fp = parse_params_1 ();
  param_depth--;
  return fp;
}

static fparams *
parse_params_1 (void)
{
  fparams *fp = XCNEW (fparams);
  next ();   /* ( */
  if (accept (")"))
    return fp;   /* unprototyped */
  fp->proto = true;
  if (is_w ("void") && is_p (")", 1))
    {
      next ();
      next ();
      return fp;
    }
  for (;;)
    {
      if (accept ("..."))
	{
	  fp->varargs = true;
	  break;
	}
      if (!decl_start_p ())
	{
	  if (peek ().kind == TZD_IDENT && fp->list.is_empty ())
	    {
	      /* K&R identifier list: f(a, b).  Unprototyped.  */
	      fp->proto = false;
	      fp->knr = true;
	      for (;;)
		{
		  if (peek ().kind != TZD_IDENT)
		    {
		      error_at (loc0 (), "expected identifier");
		      skip_parens ();
		      return fp;
		    }
		  param_info pi = { get_identifier (peek ().text), NULL_TREE,
				    loc0 () };
		  fp->list.safe_push (pi);
		  next ();
		  if (!accept (","))
		    break;
		}
	      expect (")");
	      return fp;
	    }
	  error_at (loc0 (), "expected parameter declaration");
	  skip_parens ();
	  return fp;
	}
      dspec ds;
      tree saved_pass = attr_decl_list;
      attr_decl_list = NULL_TREE;
      parse_declspecs (ds);
      tree base = finish_declspecs (ds);
      declarator d;
      memset (&d, 0, sizeof d);
      parse_declarator (d, true);
      fparams *dummy;
      tree t = apply_declarator (base, d, &dummy);
      d.ops.release ();
      if (attr_decl_list && !error_p (t) && TREE_CODE (t) == POINTER_TYPE
	  && TREE_CODE (TREE_TYPE (t)) == FUNCTION_TYPE)
	{
	  /* A calling-convention attribute on a function pointer parameter
	     (efi.h's efi_status_t (__efiapi *handler)(...)): GCC's handler
	     gives the pointer a function type that carries it, as in C.  */
	  decl_attributes (&t, attr_decl_list, 0);
	  attr_decl_list = NULL_TREE;
	}
      attr_decl_list = chainon (saved_pass, attr_decl_list);
      if (!error_p (t))
	{
	  /* Parameter adjustments.  */
	  if (va_list_p (t))
	    /* va_list stays native (GCC builtins own it).  */
	    t = build_pointer_type (TREE_TYPE (t));
	  else if (TREE_CODE (t) == ARRAY_TYPE)
	    t = make_pointer (TREE_TYPE (t));
	  else if (TREE_CODE (t) == FUNCTION_TYPE)
	    t = build_pointer_type (t);
	}
      param_info pi = { d.id, t, d.loc };
      fp->list.safe_push (pi);
      if (!accept (","))
	break;
    }
  expect (")");
  return fp;
}

static void
parse_declarator (declarator &d, bool abstract_ok)
{
  vec<dop> ptrs = vNULL, suf = vNULL, inner_ops = vNULL;
  skip_gnu_noise ();
  while (accept ("*"))
    {
      dop o = { DOP_PTR, parse_quals (), NULL_TREE, NULL };
      ptrs.safe_push (o);
    }

  if (is_p ("(") && grouping_paren_p (0))
    {
      next ();
      declarator inner;
      memset (&inner, 0, sizeof inner);
      parse_declarator (inner, abstract_ok);
      expect (")");
      d.id = inner.id;
      d.loc = inner.loc;
      d.asm_name = inner.asm_name;
      inner_ops = inner.ops;
    }
  else if (peek ().kind == TZD_IDENT)
    {
      d.id = get_identifier (peek ().text);
      d.loc = loc0 ();
      next ();
    }
  else if (!abstract_ok)
    error_at (loc0 (), "expected identifier before %qs", peek ().text);
  if (!d.loc)
    d.loc = loc0 ();

  for (;;)
    {
      if (is_p ("["))
	{
	  next ();
	  dop o = { DOP_ARRAY, 0, NULL_TREE, NULL };
	  /* C99 parameter array declarator [static N] / [const static N]:
	     the parameter is a pointer as without static (the element count
	     is only the caller's promise; the pointer keeps its bounds).  */
	  parse_quals ();
	  if (is_w ("static"))
	    {
	      next ();
	      parse_quals ();
	    }
	  /* The parameter's own array: the first [] after the name, and not
	     behind a pointer in parentheses (`u32 (*tab)[256]` is a pointer
	     to arrays of 256: the [256] is the pointee's; the kernel's
	     crc32_body).  */
	  bool outer = true;
	  for (unsigned k = 0; k < suf.length (); k++)
	    if (suf[k].kind == DOP_ARRAY)
	      outer = false;
	  for (unsigned k = 0; k < inner_ops.length (); k++)
	    if (inner_ops[k].kind != DOP_ARRAY)
	      outer = false;
	  if (!is_p ("]") && param_depth && outer)
	    {
	      /* A parameter's (outermost) array becomes a pointer: its size
		 is not needed, and may name earlier parameters (glibc's
		 regexec: regmatch_t __pmatch[_REGEX_NELTS (__nmatch)]).
		 Skipped.  Inner dimensions are part of the element type.  */
	      int depth = 0;
	      while (peek ().kind != TZD_EOF && !(depth == 0 && is_p ("]")))
		{
		  if (is_p ("[") || is_p ("("))
		    depth++;
		  else if (is_p ("]") || is_p (")"))
		    depth--;
		  next ();
		}
	    }
	  else if (!is_p ("]"))
	    {
	      if (cur_fn || param_depth)
		{
		  /* C99 variable length array: in a block the size is
		     evaluated once where the object is declared (a SAVE_EXPR,
		     see the DECL_EXPR in parse_declaration); a parameter's
		     array becomes a pointer, so its size does not matter.  */
		  location_t sl = loc0 ();
		  ex e = rvalue (parse_cond (), sl);
		  if (!bad (e))
		    {
		      tree v = fold (e.t);
		      if (TREE_CODE (v) == INTEGER_CST)
			o.size = v;
		      else if (!INTEGRAL_TYPE_P (e.type))
			error_at (sl, "size of array has non-integer type");
		      else if (cur_fn && !param_depth)
			o.size = save_expr (fold_convert (sizetype, e.t));
		    }
		}
	      else
		o.size = parse_const_int ();
	    }
	  expect ("]");
	  suf.safe_push (o);
	}
      else if (is_p ("("))
	{
	  dop o = { DOP_FUNC, 0, NULL_TREE, parse_params () };
	  suf.safe_push (o);
	}
      else
	break;
    }

  for (unsigned i = 0; i < ptrs.length (); i++)
    d.ops.safe_push (ptrs[i]);
  for (unsigned i = suf.length (); i > 0; i--)
    d.ops.safe_push (suf[i - 1]);
  for (unsigned i = 0; i < inner_ops.length (); i++)
    d.ops.safe_push (inner_ops[i]);
  ptrs.release ();
  suf.release ();
  inner_ops.release ();

  /* asm label and trailing attributes.  */
  for (;;)
    {
      if (skip_gnu_noise ())
	continue;
      if (is_w ("__asm__") || is_w ("__asm") || is_w ("asm"))
	{
	  next ();
	  expect ("(");
	  if (peek ().kind == TZD_STRING)
	    /* Adjacent literals concatenate (glibc: __asm__ ("" "name")).  */
	    d.asm_name = parse_string ().t;
	  expect (")");
	  continue;
	}
      break;
    }
}

static tree
build_fn_type (tree ret, fparams *fp)
{
  /* Managed pointers are passed and returned as value records.  */
  ret = vtype (ret);
  if (!fp->proto)
    return build_function_type (ret, NULL_TREE);
  unsigned n = fp->list.length ();
  tree *args = XALLOCAVEC (tree, n + 1);
  for (unsigned i = 0; i < n; i++)
    {
      if (error_p (fp->list[i].type))
	return error_mark_node;
      args[i] = vtype (fp->list[i].type);
    }
  /* A struct holding pointers passed or returned by value is lowered to a
     hidden pointer (sv_lower_type; TK 2026-11-09 (b), final rule #46).  */
  return fp->varargs ? build_varargs_function_type_array (ret, n, args)
		     : build_function_type_array (ret, n, args);
}

/* A type named in user code through a system-header typedef (caddr_t,
   char ** typedefs): its raw data pointers become managed, so no object
   declared in user code holds a raw pointer.  Host structs keep their
   layout (they are not rewritten).  */
static tree
user_type (tree t)
{
  if (error_p_tree (t))
    return t;
  if (raw_data_ptr_p (t))
    {
      tree s = tzd_mptr_type (user_type (TREE_TYPE (t)));
      return TYPE_QUALS (t) ? build_qualified_type (s, TYPE_QUALS (t)) : s;
    }
  if (TREE_CODE (t) == ARRAY_TYPE)
    {
      tree e = user_type (TREE_TYPE (t));
      if (e != TREE_TYPE (t))
	{
	  tree a = build_array_type (e, TYPE_DOMAIN (t));
	  return TYPE_QUALS (t) ? build_qualified_type (a, TYPE_QUALS (t)) : a;
	}
    }
  return t;
}

static tree apply_declarator_1 (tree t, declarator &d, fparams **fn);

/* The declarator's type, with a mode attribute given in the declarator.  */
static tree
apply_declarator (tree t, declarator &d, fparams **fn)
{
  return apply_attr_mode (apply_declarator_1 (t, d, fn));
}

static tree
apply_declarator_1 (tree t, declarator &d, fparams **fn)
{
  *fn = NULL;
  if (!native_region_p ())
    t = user_type (t);
  for (unsigned i = 0; i < d.ops.length (); i++)
    {
      if (error_p (t))
	return error_mark_node;
      dop &o = d.ops[i];
      *fn = NULL;
      switch (o.kind)
	{
	case DOP_PTR:
	  t = make_pointer (t);
	  if (o.quals)
	    t = build_qualified_type (t, o.quals);
	  break;
	case DOP_ARRAY:
	  if (TREE_CODE (t) == FUNCTION_TYPE)
	    {
	      error_at (d.loc, "declaration of array of functions");
	      return error_mark_node;
	    }
	  if (o.size)
	    {
	      if (TREE_CODE (o.size) == INTEGER_CST
		  && tree_int_cst_sgn (o.size) < 0)
		{
		  error_at (d.loc, "size of array is negative");
		  return error_mark_node;
		}
	      tree max = size_binop (MINUS_EXPR, fold_convert (sizetype,
							       o.size),
				     size_one_node);
	      if (!array_slot_check (t, d.loc))
		return error_mark_node;
	      t = build_array_type (t, build_index_type (max));
	    }
	  else
	    {
	      if (!array_slot_check (t, d.loc))
		return error_mark_node;
	      t = build_array_type (t, NULL_TREE);
	    }
	  break;
	case DOP_FUNC:
	  if (TREE_CODE (t) == FUNCTION_TYPE || TREE_CODE (t) == ARRAY_TYPE)
	    {
	      error_at (d.loc, "function cannot return a function or array");
	      return error_mark_node;
	    }
	  t = build_fn_type (TYPE_MAIN_VARIANT (t) == void_type_node
			     ? void_type_node : t, o.fp);
	  *fn = o.fp;
	  break;
	}
    }
  return t;
}

static tree
parse_type_name (void)
{
  dspec ds;
  parse_declspecs (ds);
  tree base = finish_declspecs (ds);
  declarator d;
  memset (&d, 0, sizeof d);
  parse_declarator (d, true);
  fparams *fp;
  tree t = apply_declarator (base, d, &fp);
  d.ops.release ();
  return t;
}

/* ------------------------------------------------------------------ */
/* Expressions: primary and postfix.  */

static ex
parse_number (const tzd_token &t)
{
  const char *s = t.text;
  size_t n = strlen (s);
  bool hex = n > 1 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
  bool flt = false;
  for (size_t i = 0; i < n; i++)
    if (s[i] == '.' || (!hex && (s[i] == 'e' || s[i] == 'E'))
	|| (hex && (s[i] == 'p' || s[i] == 'P')))
      flt = true;

  if (flt)
    {
      tree type = double_type_node;
      char *buf = xstrdup (s);
      size_t m = n;
      while (m > 0 && strchr ("fFlL", buf[m - 1]))
	{
	  if (buf[m - 1] == 'f' || buf[m - 1] == 'F')
	    type = float_type_node;
	  else
	    type = long_double_type_node;
	  buf[--m] = 0;
	}
      REAL_VALUE_TYPE r;
      real_from_string3 (&r, buf, TYPE_MODE (type));
      free (buf);
      return mk (build_real (type, r), type);
    }

  unsigned HOST_WIDE_INT v = 0;
  bool overflow = false;
  size_t i = 0;
  int base = 10;
  if (hex)
    {
      base = 16;
      i = 2;
    }
  else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))
    {
      /* 0b1010 (C23, GNU before it).  */
      base = 2;
      i = 2;
    }
  else if (s[0] == '0')
    base = 8;
  for (; i < n; i++)
    {
      int c = s[i], dv;
      if (c >= '0' && c <= '9')
	dv = c - '0';
      else if (base == 16 && c >= 'a' && c <= 'f')
	dv = c - 'a' + 10;
      else if (base == 16 && c >= 'A' && c <= 'F')
	dv = c - 'A' + 10;
      else
	break;
      if (dv >= base)
	{
	  error_at (t.loc, "invalid digit in integer constant %qs", s);
	  return err ();
	}
      unsigned HOST_WIDE_INT nv = v * base + dv;
      if (nv / base != v)
	overflow = true;
      v = nv;
    }
  int nl = 0;
  bool u = false;
  for (; i < n; i++)
    {
      if (s[i] == 'u' || s[i] == 'U')
	u = true;
      else if (s[i] == 'l' || s[i] == 'L')
	nl++;
      else
	{
	  error_at (t.loc, "invalid suffix on integer constant %qs", s);
	  return err ();
	}
    }
  if (overflow)
    warning_at (t.loc, 0, "integer constant is too large");

  tree cands[6];
  int nc = 0;
  bool dec = base == 10;
  if (nl == 0)
    {
      if (!u)
	cands[nc++] = integer_type_node;
      if (u || !dec)
	cands[nc++] = unsigned_type_node;
    }
  if (nl <= 1)
    {
      if (!u)
	cands[nc++] = long_integer_type_node;
      if (u || !dec)
	cands[nc++] = long_unsigned_type_node;
    }
  if (!u)
    cands[nc++] = long_long_integer_type_node;
  cands[nc++] = long_long_unsigned_type_node;

  for (int k = 0; k < nc; k++)
    {
      unsigned prec = TYPE_PRECISION (cands[k]) - !TYPE_UNSIGNED (cands[k]);
      if (prec >= HOST_BITS_PER_WIDE_INT || v < (HOST_WIDE_INT_1U << prec))
	return mk (build_int_cstu (cands[k], v), cands[k]);
    }
  return mk (build_int_cstu (long_long_unsigned_type_node, v),
	     long_long_unsigned_type_node);
}

static ex
parse_string (void)
{
  /* Wide, UTF-16, UTF-32 literals (L"", u"", U""), alone or concatenated
     with narrow ones: all interpreted together in the widest kind, as
     C does; the array's elements are of that kind's type.  */
  {
    int kind = CPP_STRING;
    unsigned k = tpos;
    for (; k < toks.length () && toks[k].kind == TZD_STRING; k++)
      if (toks[k].strtype != CPP_STRING && toks[k].strtype != CPP_UTF8STRING)
	{
	  if (kind != CPP_STRING && kind != toks[k].strtype)
	    {
	      error_at (loc0 (), "concatenation of different kinds of wide "
			"string literals");
	      kind = toks[k].strtype;
	    }
	  kind = toks[k].strtype;
	}
    if (kind != CPP_STRING)
      {
	location_t loc = loc0 ();
	unsigned n = k - tpos;
	cpp_string *in = XALLOCAVEC (cpp_string, n);
	for (unsigned i = 0; i < n; i++)
	  {
	    in[i].len = toks[tpos + i].rawlen;
	    in[i].text = toks[tpos + i].raw;
	  }
	tree elt = kind == CPP_WSTRING ? wchar_type_node
		   : kind == CPP_STRING16 ? char16_type_node
		   : char32_type_node;
	cpp_string out;
	tpos = k;
	if (!cpp_interpret_string (parse_in, in, n, &out, (cpp_ttype) kind))
	  return err ();
	unsigned width = TYPE_PRECISION (elt) / BITS_PER_UNIT;
	tree s = build_string (out.len, (const char *) out.text);
	free (CONST_CAST (unsigned char *, out.text));
	tree atype = build_array_type (elt,
				       build_index_type
					 (size_int (out.len / width - 1)));
	TREE_TYPE (s) = atype;
	TREE_CONSTANT (s) = 1;
	TREE_READONLY (s) = 1;
	TREE_STATIC (s) = 1;
	(void) loc;
	return mk (s, atype, true);
      }
  }
  obstack ob;
  gcc_obstack_init (&ob);
  location_t loc = loc0 ();
  while (peek ().kind == TZD_STRING)
    {
      obstack_grow (&ob, peek ().str, peek ().len);
      next ();
    }
  size_t len = obstack_object_size (&ob);
  obstack_1grow (&ob, 0);
  const char *p = (const char *) obstack_finish (&ob);
  tree s = build_string (len + 1, p);
  obstack_free (&ob, NULL);
  tree atype = build_array_type (char_type_node,
				 build_index_type (size_int (len)));
  TREE_TYPE (s) = atype;
  TREE_CONSTANT (s) = 1;
  TREE_READONLY (s) = 1;
  TREE_STATIC (s) = 1;
  (void) loc;
  return mk (s, atype, true);
}

static ex
parse_primary (void)
{
  const tzd_token &t = peek ();
  location_t loc = t.loc;
  switch (t.kind)
    {
    case TZD_NUMBER:
      {
	ex e = parse_number (t);
	e.ice = !bad (e) && INTEGRAL_TYPE_P (e.type);
	next ();
	return e;
      }
    case TZD_CHAR:
      {
	ex e = mk (build_int_cst (integer_type_node, t.cval),
		   integer_type_node);
	e.ice = true;
	next ();
	return e;
      }
    case TZD_STRING:
      return parse_string ();
    case TZD_KEYWORD:
      /* TzD (.d) literals.  true/false are bool constants; null is the
	 null pointer (a void pointer with no allocation), not the integer
	 0, so `int x = null` is a type error.  */
      if (strcmp (t.text, "true") == 0 || strcmp (t.text, "false") == 0)
	{
	  bool v = t.text[0] == 't';
	  next ();
	  return mk (build_int_cst (boolean_type_node, v), boolean_type_node);
	}
      if (strcmp (t.text, "null") == 0)
	{
	  next ();
	  tree vp = tzd_mptr_type (void_type_node);
	  return mk_parts (vp, null_pointer_node, null_pointer_node,
			   size_zero_node);
	}
      error_at (loc, "expected expression before %qs", t.text);
      next ();
      return err ();
    case TZD_IDENT:
      {
	/* C23 nullptr (-std=gnu23, task #71: coreutils 9.11): the null
	   pointer constant, here a managed void pointer with no allocation
	   (as `null` in .d); typeof (nullptr) is that pointer type, which
	   converts to every pointer type as nullptr_t does.  */
	if (flag_isoc23 && strcmp (t.text, "nullptr") == 0)
	  {
	    next ();
	    tree vp = tzd_mptr_type (void_type_node);
	    return mk_parts (vp, null_pointer_node, null_pointer_node,
			     size_zero_node);
	  }
	if (strcmp (t.text, "__func__") == 0
	    || strcmp (t.text, "__FUNCTION__") == 0
	    || strcmp (t.text, "__PRETTY_FUNCTION__") == 0)
	  {
	    /* The enclosing function's name as a string (assert uses it).  */
	    next ();
	    const char *nm = (cur_fn && DECL_NAME (cur_fn->decl))
			     ? IDENTIFIER_POINTER (DECL_NAME (cur_fn->decl)) : "";
	    size_t len = strlen (nm);
	    tree str = build_string (len + 1, nm);
	    tree atype = build_array_type (char_type_node,
					   build_index_type (size_int (len)));
	    TREE_TYPE (str) = atype;
	    TREE_CONSTANT (str) = 1;
	    TREE_READONLY (str) = 1;
	    TREE_STATIC (str) = 1;
	    return mk (str, atype, true);
	  }
	if (strcmp (t.text, "__tzd_fncast") == 0)
	  {
	    /* __tzd_fncast (T, e): reinterpret the generic function pointer e
	       as the interface T (a function type or pointer to one), after a
	       run-time check that e was registered -- where a named function
	       was erased to void(*)(void) -- with T's interface id, else trap.
	       The generic->typed conversion cannot be verified statically, so
	       #22 is resolved by enforcing the declared contract dynamically
	       rather than by weakening the compiler (task #61).  */
	    next ();
	    expect ("(");
	    tree it = parse_type_name ();
	    expect (",");
	    ex e = parse_assign ();
	    expect (")");
	    e = rvalue (e, loc);
	    if (!it || it == error_mark_node || bad (e))
	      return err ();
	    tree fnty = it;
	    if (TZD_MPTR_P (fnty) || TREE_CODE (fnty) == POINTER_TYPE)
	      fnty = TZD_MPTR_P (fnty) ? TZD_MPTR_TARGET (fnty)
				      : TREE_TYPE (fnty);
	    if (TREE_CODE (fnty) != FUNCTION_TYPE)
	      {
		error_at (loc, "%<__tzd_fncast%> needs a function or "
			  "function-pointer type as its first argument");
		return err ();
	      }
	    if (!POINTER_TYPE_P (e.type) && !TZD_MPTR_P (e.type))
	      {
		error_at (loc, "%<__tzd_fncast%> needs a function pointer as "
			  "its second argument");
		return err ();
	      }
	    tree rty = build_pointer_type (fnty);
	    init_shadow_decls ();
	    tree v = save_expr (value (e));
	    unsigned long id = tzd_fn_interface_id (fnty);
	    tree chk = build_call_expr_loc (loc, tzd_fn_check_decl, 2,
					    fold_convert (const_ptr_type_node, v),
					    build_int_cstu (long_unsigned_type_node,
							    id));
	    return mk (build2 (COMPOUND_EXPR, rty, chk, fold_convert (rty, v)),
		       rty);
	  }
	if (strcmp (t.text, "__tzd_narrow") == 0)
	  {
	    /* __tzd_narrow (p, n): p with bounds [p, p + n).  Traps unless
	       those n bytes lie inside p's current bounds, so it can only
	       narrow, never widen: no trust is needed (allocators hand out
	       parts of a block with it, task #13).  */
	    next ();
	    expect ("(");
	    ex p = parse_assign ();
	    expect (",");
	    ex n = parse_assign ();
	    expect (")");
	    p = rvalue (p, loc);
	    n = rvalue (n, loc);
	    if (bad (p) || bad (n))
	      return err ();
	    if (!TZD_MPTR_P (p.type) || !INTEGRAL_TYPE_P (n.type))
	      {
		error_at (loc, "%<__tzd_narrow%> takes a managed pointer and "
			  "a size");
		return err ();
	      }
	    parts (p);
	    tree pa = save_expr (p.pa), pb = save_expr (p.pb);
	    tree ps = save_expr (fold_convert (size_type_node, p.ps));
	    tree nn = save_expr (fold_convert (size_type_node, value (n)));
	    tree chk = build_check (pa, pb, ps, nn, loc);
	    tree addr = chk ? build2 (COMPOUND_EXPR, TREE_TYPE (pa), chk, pa)
			    : pa;
	    return mk_parts (p.type, addr, pa, nn);
	  }
	if (strcmp (t.text, "__tzd_wipe") == 0)
	  {
	    /* __tzd_wipe (p, n): destroy [p, p + n) as a release does --
	       zero, then random bytes, bounds entries removed (tzd_sh_wipe).
	       Traps unless those n bytes lie inside p's bounds.  For
	       allocators that recycle memory without free (task #3).  */
	    next ();
	    expect ("(");
	    ex p = parse_assign ();
	    expect (",");
	    ex n = parse_assign ();
	    expect (")");
	    p = rvalue (p, loc);
	    n = rvalue (n, loc);
	    if (bad (p) || bad (n))
	      return err ();
	    if (!TZD_MPTR_P (p.type) || !INTEGRAL_TYPE_P (n.type))
	      {
		error_at (loc, "%<__tzd_wipe%> takes a managed pointer and "
			  "a size");
		return err ();
	      }
	    parts (p);
	    tree pa = save_expr (p.pa), pb = save_expr (p.pb);
	    tree ps = save_expr (fold_convert (size_type_node, p.ps));
	    tree nn = save_expr (fold_convert (size_type_node, value (n)));
	    tree chk = build_check (pa, pb, ps, nn, loc);
	    init_shadow_decls ();
	    /* In user space a recycling wipe is not the end of an object's
	       storage: pointers kept to the range are not marked dangling
	       (libc2d tzd_wipe_range; the kernel's kc2d has tzd_sh_wipe).  */
	    tree wd = tzd_sh_wipe_decl;
	    if (!flag_tzd_kernel)
	      wd = libd_decl ("tzd_wipe_range", TREE_TYPE (tzd_sh_wipe_decl));
	    tree w = build_call_expr_loc (loc, wd, 2,
					  fold_convert (ptr_type_node, pa), nn);
	    if (chk)
	      w = build2 (COMPOUND_EXPR, void_type_node, chk, w);
	    return mk (w, void_type_node);
	  }
	if (strcmp (t.text, "__tzd_copy") == 0)
	  {
	    /* __tzd_copy (d, s, n): memmove of N bytes (libc2d's memcpy and
	       memmove, task #8).  Both ranges are checked like any access
	       (bounds, live heap block); then the bytes, the bounds entries
	       of their pointer slots and the heap map's written bits move
	       together (libc2d tzd_copy_bytes).  Copying is not reading:
	       bytes never written stay "never written" at D.  */
	    next ();
	    expect ("(");
	    ex d = parse_assign ();
	    expect (",");
	    ex s = parse_assign ();
	    expect (",");
	    ex n = parse_assign ();
	    expect (")");
	    d = rvalue (d, loc);
	    s = rvalue (s, loc);
	    n = rvalue (n, loc);
	    if (bad (d) || bad (s) || bad (n))
	      return err ();
	    if (!TZD_MPTR_P (d.type) || !TZD_MPTR_P (s.type)
		|| !INTEGRAL_TYPE_P (n.type))
	      {
		error_at (loc, "%<__tzd_copy%> takes two managed pointers and "
			  "a size");
		return err ();
	      }
	    parts (d);
	    parts (s);
	    tree da = save_expr (d.pa), db = save_expr (d.pb);
	    tree ds = save_expr (fold_convert (size_type_node, d.ps));
	    tree sa = save_expr (s.pa), sb = save_expr (s.pb);
	    tree ss = save_expr (fold_convert (size_type_node, s.ps));
	    tree nn = save_expr (fold_convert (size_type_node, value (n)));
	    init_hp_decls ();
	    tree r = build_call_expr_loc (loc, copy_bytes_decl, 3,
					  fold_convert (ptr_type_node, da),
					  fold_convert (const_ptr_type_node, sa),
					  nn);
	    if (tree c = build_check (sa, sb, ss, nn, loc))
	      r = build2 (COMPOUND_EXPR, void_type_node, c, r);
	    if (grp_active_p ())
	      r = build2 (COMPOUND_EXPR, void_type_node,
			  grp_guard_addr (da, loc), r);
	    if (tree c = build_check (da, db, ds, nn, loc))
	      r = build2 (COMPOUND_EXPR, void_type_node, c, r);
	    return mk (r, void_type_node);
	  }
	if (strcmp (t.text, "__builtin_offsetof") == 0)
	  {
	    next ();
	    expect ("(");
	    tree type = parse_type_name ();
	    expect (",");
	    if (error_p (type))
	      return err ();
	    ex b = mk (build1 (INDIRECT_REF, type,
			       build_int_cst (build_pointer_type (type), 0)),
		       type, true);
	    b.nullbase = true;
	    bool first = true;
	    for (;;)
	      {
		if (first || accept ("."))
		  {
		    if (peek ().kind != TZD_IDENT)
		      {
			error_at (loc0 (), "expected identifier");
			break;
		      }
		    tree id = get_identifier (peek ().text);
		    location_t mloc = loc0 ();
		    next ();
		    b = member (b, id, mloc);
		    first = false;
		  }
		else if (accept ("["))
		  {
		    ex i = rvalue (parse_expr (), loc);
		    expect ("]");
		    if (bad (b) || bad (i) || TREE_CODE (b.type) != ARRAY_TYPE)
		      {
			error_at (loc, "invalid array designator in offsetof");
			return err ();
		      }
		    tree ref = build4 (ARRAY_REF, TREE_TYPE (b.type), b.t,
				       fold_convert (sizetype, i.t), NULL_TREE,
				       NULL_TREE);
		    bool nb = b.nullbase;
		    b = mk (ref, TREE_TYPE (b.type), true);
		    b.nullbase = nb;
		  }
		else
		  break;
		if (bad (b))
		  break;
	      }
	    expect (")");
	    if (bad (b))
	      return err ();
	    return mk (fold_offsetof (b.t), size_type_node);
	  }
	if (strcmp (t.text, "__builtin_has_attribute") == 0)
	  {
	    /* __builtin_has_attribute (expr-or-type, name): whether the
	       attribute is on it as cc1tzd keeps it -- hints it drops
	       (nonstring, ...) are not there.  Not evaluated.  */
	    next ();
	    expect ("(");
	    tree node = NULL_TREE;
	    if (typename_start_p (0))
	      node = parse_type_name ();
	    else
	      {
		unevaluated++;
		ex e = parse_assign ();
		unevaluated--;
		if (!bad (e))
		  {
		    node = e.t;
		    while (node && (TREE_CODE (node) == NOP_EXPR
				    || TREE_CODE (node) == VIEW_CONVERT_EXPR))
		      node = TREE_OPERAND (node, 0);
		    if (node && TREE_CODE (node) == COMPONENT_REF)
		      node = TREE_OPERAND (node, 1);
		    if (!node || !DECL_P (node))
		      node = e.type;
		  }
	      }
	    expect (",");
	    char ab[64];
	    const char *an = "";
	    if (peek ().kind == TZD_IDENT || peek ().kind == TZD_KEYWORD)
	      {
		an = attr_bare_name (peek ().text, ab, sizeof ab);
		next ();
	      }
	    if (is_p ("("))
	      skip_parens ();
	    expect (")");
	    bool has = false;
	    if (node && !error_p_tree (node))
	      {
		if (strcmp (an, "aligned") == 0)
		  has = DECL_P (node) ? DECL_USER_ALIGN (node)
				      : TYPE_USER_ALIGN (node);
		else if (strcmp (an, "packed") == 0)
		  has = DECL_P (node) ? DECL_PACKED (node) : TYPE_PACKED (node);
		else
		  has = lookup_attribute (an, DECL_P (node)
					      ? DECL_ATTRIBUTES (node)
					      : TYPE_ATTRIBUTES (node))
			!= NULL_TREE;
	      }
	    ex r = mk (build_int_cst (integer_type_node, has), integer_type_node);
	    r.ice = true;
	    return r;
	  }
	if (strcmp (t.text, "__builtin_choose_expr") == 0)
	  {
	    /* __builtin_choose_expr (const, a, b): a or b, chosen while
	       parsing; the other one is not evaluated.  */
	    next ();
	    expect ("(");
	    tree c = parse_const_int ();
	    expect (",");
	    bool first = c && !integer_zerop (c);
	    if (!first)
	      unevaluated++;
	    ex a = parse_assign ();
	    if (!first)
	      unevaluated--;
	    expect (",");
	    if (first)
	      unevaluated++;
	    ex b = parse_assign ();
	    if (first)
	      unevaluated--;
	    expect (")");
	    if (!c)
	      return err ();
	    return first ? a : b;
	  }
	if (strcmp (t.text, "__builtin_types_compatible_p") == 0)
	  {
	    next ();
	    expect ("(");
	    tree a = parse_type_name ();
	    expect (",");
	    tree b = parse_type_name ();
	    expect (")");
	    if (error_p (a) || error_p (b))
	      return err ();
	    /* Top-level qualifiers are ignored (as GCC).  */
	    bool c = tzd_types_compatible_p (TYPE_MAIN_VARIANT (tzd_lang_type (a)),
					     TYPE_MAIN_VARIANT (tzd_lang_type (b)));
	    ex r = mk (build_int_cst (integer_type_node, c), integer_type_node);
	    r.ice = true;
	    return r;
	  }
	if (strcmp (t.text, "_Generic") == 0)
	  {
	    /* C11 generic selection: the controlling expression is not
	       evaluated; its type after lvalue conversion (qualifiers
	       dropped, arrays and functions decayed) picks one
	       association.  */
	    next ();
	    expect ("(");
	    unevaluated++;
	    ex c = parse_assign ();
	    unevaluated--;
	    tree ct = error_mark_node;
	    if (!bad (c))
	      {
		ct = c.type;
		if (TREE_CODE (ct) == ARRAY_TYPE)
		  ct = tzd_mptr_type (TREE_TYPE (ct));
		else if (TREE_CODE (ct) == FUNCTION_TYPE)
		  ct = build_pointer_type (ct);
		ct = TYPE_MAIN_VARIANT (tzd_lang_type (ct));
	      }
	    ex chosen = err (), dflt = err ();
	    bool found = false, have_dflt = false;
	    while (accept (","))
	      {
		bool is_default = false;
		tree at = NULL_TREE;
		if (is_w ("default"))
		  {
		    next ();
		    is_default = true;
		  }
		else
		  at = parse_type_name ();
		expect (":");
		ex e = parse_assign ();
		if (is_default)
		  {
		    dflt = e;
		    have_dflt = true;
		  }
		else if (!found && at && !error_p (at) && !error_p (ct)
			 && tzd_types_compatible_p (ct, at))
		  {
		    chosen = e;
		    found = true;
		  }
	      }
	    expect (")");
	    if (error_p (ct))
	      return err ();
	    if (found)
	      return chosen;
	    if (have_dflt)
	      return dflt;
	    error_at (t.loc, "%<_Generic%> selector of type %qT is not "
		      "compatible with any association", ct);
	    return err ();
	  }
	if (strcmp (t.text, "__builtin_va_arg") == 0)
	  {
	    next ();
	    expect ("(");
	    ex ap = parse_assign ();
	    expect (",");
	    tree type = parse_type_name ();
	    expect (")");
	    if (bad (ap) || error_p (type))
	      return err ();
	    /* Managed pointers travel through varargs as value records.  */
	    return mk (build_va_arg (loc, ap.t, vtype (type)), type);
	  }
	tree id = get_identifier (t.text);
	tree d = tzd_lookup (id);
	next ();
	if (d == NULL_TREE)
	  {
	    if (is_p ("("))
	      {
		/* C89 implicit declaration: int name ().  */
		warning_at (loc, 0, "implicit declaration of function %qE", id);
		tree ft = build_function_type (integer_type_node, NULL_TREE);
		d = build_decl (loc, FUNCTION_DECL, id, ft);
		DECL_EXTERNAL (d) = 1;
		TREE_PUBLIC (d) = 1;
		pushdecl (d);
	      }
	    else
	      {
		error_at (loc, "%qE undeclared", id);
		return err ();
	      }
	  }
	switch (TREE_CODE (d))
	  {
	  case VAR_DECL:
	  case PARM_DECL:
	    TREE_USED (d) = 1;
	    return mk (d, TREE_TYPE (d), true);
	  case FUNCTION_DECL:
	    TREE_USED (d) = 1;
	    return mk (d, fn_lang_type (d));
	  case CONST_DECL:
	    {
	      ex e = mk (DECL_INITIAL (d), TREE_TYPE (d));
	      e.ice = true;
	      return e;
	    }
	  default:
	    error_at (loc, "unexpected type name %qE", id);
	    return err ();
	  }
      }
    case TZD_PUNCT:
      if (is_p ("("))
	{
	  next ();
	  if (is_p ("{"))
	    {
	      /* GNU statement expression ({ ...; v; }) (glibc's ctype macros
		 at -O2).  The BIND_EXPR gets the type of its last
		 expression statement; the gimplifier makes the value.  */
	      if (cur_fn == NULL)
		{
		  error_at (loc, "braced-group within expression allowed only "
			    "inside a function");
		  return err ();
		}
	      int saved_depth = stmtexpr_depth;
	      tree saved_type = stmtexpr_type;
	      tree saved_res = stmtexpr_res;
	      int saved_sub = stmtexpr_sub_depth;
	      stmtexpr_sub_depth = sub_stmt_depth;
	      stmtexpr_depth = compound_depth + 1;
	      stmtexpr_type = void_type_node;
	      stmtexpr_res = NULL_TREE;
	      tree bind = NULL_TREE;
	      parse_compound (&bind, false, true);
	      tree type = stmtexpr_type;
	      tree res = stmtexpr_res;
	      stmtexpr_depth = saved_depth;
	      stmtexpr_type = saved_type;
	      stmtexpr_res = saved_res;
	      stmtexpr_sub_depth = saved_sub;
	      expect (")");
	      if (bind == NULL_TREE)
		return err ();
	      if (res)
		{
		  /* The object lives in the enclosing block (wiped there).  */
		  TREE_TYPE (bind) = void_type_node;
		  cl_wipe_add (res);
		  tree te = build4 (TARGET_EXPR, type, res, bind, NULL_TREE,
				    NULL_TREE);
		  TREE_SIDE_EFFECTS (te) = 1;
		  SET_EXPR_LOCATION (te, loc);
		  return mk (te, type);
		}
	      TREE_TYPE (bind) = vtype (type);
	      return mk (bind, type);
	    }
	  ex e = parse_expr ();
	  expect (")");
	  return e;
	}
      /* FALLTHRU */
    default:
      error_at (loc, "expected expression before %qs", t.text);
      next ();
      return err ();
    }
}

/* Does struct/union ST have member ID (directly or via anonymous members)?  */
static bool
has_member (tree st, tree id)
{
  for (tree f = TYPE_FIELDS (st); f; f = DECL_CHAIN (f))
    if (TREE_CODE (f) != FIELD_DECL)
      continue;
    else if (DECL_NAME (f) == id)
      return true;
    else if (DECL_NAME (f) == NULL_TREE
	     && RECORD_OR_UNION_TYPE_P (TREE_TYPE (f))
	     && has_member (TREE_TYPE (f), id))
      return true;
  return false;
}

/* F is the first link of a BSD queue.h TAILQ link pair ST: exactly two
   pointer fields named tqe_next/tqe_prev (TAILQ_ENTRY) or
   tqh_first/tqh_last (TAILQ_HEAD).  */
static bool
queue_link_first_p (tree st, tree f)
{
  if (TREE_CODE (st) != RECORD_TYPE || !DECL_NAME (f))
    return false;
  tree f2 = DECL_CHAIN (f);
  while (f2 && TREE_CODE (f2) != FIELD_DECL)
    f2 = DECL_CHAIN (f2);
  if (TYPE_FIELDS (st) != f || !f2 || !DECL_NAME (f2) || DECL_CHAIN (f2))
    return false;
  const char *a = IDENTIFIER_POINTER (DECL_NAME (f));
  const char *b = IDENTIFIER_POINTER (DECL_NAME (f2));
  if (!((strcmp (a, "tqe_next") == 0 && strcmp (b, "tqe_prev") == 0)
	|| (strcmp (a, "tqh_first") == 0 && strcmp (b, "tqh_last") == 0)))
    return false;
  return TZD_MSLOT_P (TREE_TYPE (f)) && TZD_MSLOT_P (TREE_TYPE (f2));
}

static ex
member (ex e, tree id, location_t loc)
{
  if (bad (e))
    return e;
  tree st = e.type;
  if (!RECORD_OR_UNION_TYPE_P (st) || TZD_MPTR_P (st))
    {
      error_at (loc, "request for member %qE in something not a structure "
		"or union", id);
      return err ();
    }
  if (!COMPLETE_TYPE_P (st))
    {
      error_at (loc, "invalid use of incomplete struct type");
      return err ();
    }
  for (tree f = TYPE_FIELDS (st); f; f = DECL_CHAIN (f))
    if (DECL_NAME (f) == NULL_TREE && TREE_CODE (f) == FIELD_DECL
	&& RECORD_OR_UNION_TYPE_P (TREE_TYPE (f))
	&& has_member (TREE_TYPE (f), id))
      {
	/* Anonymous struct/union member: go through it.  It keeps the
	   parent's bounds; the named member below narrows them.  */
	tree ft = TREE_TYPE (f);
	int q = TYPE_QUALS (st);
	if (q)
	  ft = build_qualified_type (ft, TYPE_QUALS (ft) | q);
	tree ref = build3 (COMPONENT_REF, ft, e.t, f, NULL_TREE);
	if (TREE_SIDE_EFFECTS (e.t))
	  TREE_SIDE_EFFECTS (ref) = 1;
	ex r = mk (ref, ft, e.lv);
	r.ob = e.ob;
	r.os = e.os;
	r.nullbase = e.nullbase;
	return member (r, id, loc);
      }
    else if (DECL_NAME (f) == id)
      {
	tree ft = TREE_TYPE (f);
	int q = TYPE_QUALS (st);
	if (q)
	  ft = build_qualified_type (ft, TYPE_QUALS (ft) | q);
	tree ref = build3 (COMPONENT_REF, ft, e.t, f, NULL_TREE);
	if (TREE_SIDE_EFFECTS (e.t))
	  TREE_SIDE_EFFECTS (ref) = 1;
	ex r = mk (ref, ft, e.lv);
	r.nullbase = e.nullbase;
	if (RECORD_OR_UNION_TYPE_P (ft) && integer_zerop (byte_position (f)))
	  {
	    /* A struct as the first member is C-style inheritance: code
	       casts &outer->base back to the outer type (Apache's
	       psprintf_data / ap_vformatter_buff).  Keep the parent's bounds
	       (provisional, task #21).  */
	    r.ob = e.ob;
	    r.os = e.os;
	  }
	else if (type_name_str (st)
		 && tzd_whole_member_p (type_name_str (st),
					IDENTIFIER_POINTER (id)))
	  {
	    /* #pragma tzd whole_member (task #70): the member's address
	       keeps the parent's bounds (bison's union yyalloc).  */
	    r.ob = e.ob;
	    r.os = e.os;
	  }
	else if (queue_link_first_p (st, f))
	  {
	    /* BSD <sys/queue.h> TAILQ: tqh_last / tqe_prev hold &X.first
	       and TAILQ_LAST / TAILQ_PREV cast it back to the head type
	       to read its second link (OpenSSH sshconnect2.c).  The two
	       link structs share one layout by design, so &X.first gets
	       the bounds of the link pair X -- not of the enclosing
	       object; the other members stay protected.  F is at offset
	       0, so the pair starts at &X.first itself.  */
	    r.sub = true;
	    r.subsize = TYPE_SIZE_UNIT (st);
	  }
	else if (flag_tzd_kernel && RECORD_OR_UNION_TYPE_P (ft))
	  {
	    /* Kernel mode: an embedded struct or union (list_head, rb_node,
	       work_struct, ...) is how the kernel links objects, and
	       container_of goes back from it to the enclosing object
	       (list_for_each_entry).  It keeps the parent's bounds; its
	       array and scalar members are narrowed when named
	       (p->inner.buf[i]).  要確認 (task #42, #21).  */
	    r.ob = e.ob;
	    r.os = e.os;
	  }
	else if (integer_zerop (byte_position (f))
		 && TYPE_SIZE_UNIT (ft) && TYPE_SIZE_UNIT (st)
		 && tree_int_cst_equal (TYPE_SIZE_UNIT (ft),
					TYPE_SIZE_UNIT (st)))
	  {
	    /* The member is the whole struct (a wrapper such as the
	       kernel's pgd_t { pgdval_t pgd; }): &pgt->pgd steps through
	       the array of wrappers (pgd[i]), and there is nothing else in
	       the struct to protect.  Keep the parent's bounds.  */
	    r.ob = e.ob;
	    r.os = e.os;
	  }
	else if (TREE_CODE (st) == RECORD_TYPE && last_field_p (f)
		 && (ends_in_fam_p (ft) || RECORD_OR_UNION_TYPE_P (ft)))
	  {
	    /* A struct that ends in a flexible array, as the last member
	       (pci_cap_saved_state's cap.data[]): its array reaches the end
	       of the enclosing allocation, so keep the parent's bounds.
	       Likewise any struct or union as the last member: a header
	       followed by data in the same allocation (busybox's
	       nlmsg_list.h, filled with memcpy (&h->h, n, n->nlmsg_len)).
	       No member follows it to protect.  */
	    r.ob = e.ob;
	    r.os = e.os;
	  }
	else
	  r.sub = true;
	/* Flexible-array idiom: last field, array of unknown size, 0 or 1.  */
	if (TREE_CODE (st) == RECORD_TYPE && TREE_CODE (ft) == ARRAY_TYPE)
	  {
	    tree rest = DECL_CHAIN (f);
	    while (rest && TREE_CODE (rest) != FIELD_DECL)
	      rest = DECL_CHAIN (rest);
	    tree dom = TYPE_DOMAIN (ft);
	    if (rest == NULL_TREE
		&& (dom == NULL_TREE || TYPE_MAX_VALUE (dom) == NULL_TREE
		    /* [0]: the bound is sizetype -1, which is unsigned.  */
		    || (TYPE_SIZE_UNIT (ft)
			&& integer_zerop (TYPE_SIZE_UNIT (ft)))
		    || (TREE_CODE (TYPE_MAX_VALUE (dom)) == INTEGER_CST
			&& tree_int_cst_sgn (TYPE_MAX_VALUE (dom)) <= 0)))
	      {
		r.tail = true;
		/* It reaches the end of the allocation: not narrowed to
		   the member (mem_section_usage's pageblock_flags[0]).  */
		r.sub = false;
		r.ob = e.ob;
		r.os = e.os;
	      }
	  }
	return r;
      }
  error_at (loc, "no member named %qE", id);
  return err ();
}

/* ISO C functions declared by the host's system headers that libc2d
   implements with bounds checks (task #14, provisional).  Calls to them
   from Managed C go to libc2d with the managed pointers, so an overflow
   inside memcpy/strcpy/... traps instead of corrupting memory, and malloc
   returns a pointer that carries its allocation bounds.  */
static const char *const libd_redirect_names[][2] = {
  { "memcpy", "tzd_memcpy" }, { "memmove", "tzd_memmove" },
  { "memset", "tzd_memset" }, { "memcmp", "tzd_memcmp" },
  { "memchr", "tzd_memchr" }, { "strlen", "tzd_strlen" },
  { "strcpy", "tzd_strcpy" }, { "strncpy", "tzd_strncpy" },
  { "strcat", "tzd_strcat" }, { "strncat", "tzd_strncat" },
  { "strchr", "tzd_strchr" }, { "strrchr", "tzd_strrchr" },
  { "strstr", "tzd_strstr" }, { "strcmp", "tzd_strcmp" },
  { "strncmp", "tzd_strncmp" }, { "strcasecmp", "tzd_strcasecmp" },
  /* Through libc2d's allocator, so free() knows the block.  */
  { "strdup", "tzd_strdup" }, { "strndup", "tzd_strndup" },
  /* GNU/POSIX string functions: the host's read and write unchecked.  */
  { "mempcpy", "tzd_mempcpy" }, { "stpcpy", "tzd_stpcpy" },
  { "stpncpy", "tzd_stpncpy" }, { "strchrnul", "tzd_strchrnul" },
  { "strpbrk", "tzd_strpbrk" }, { "strtok_r", "tzd_strtok_r" },
  { "bsearch", "tzd_bsearch" },
  { "strcasestr", "tzd_strcasestr" }, { "strsep", "tzd_strsep" },
  /* realpath (p, NULL) returns a host malloc block: libc2d copies it into
     its own, so free knows it.  */
  { "realpath", "tzd_realpath" },
  /* GNU/C11 additions coreutils (gnulib) uses.  */
  { "canonicalize_file_name", "tzd_canonicalize_file_name" },
  { "getcwd", "tzd_getcwd" },
  { "aligned_alloc", "tzd_aligned_alloc" },
  { "memrchr", "tzd_memrchr" }, { "rawmemchr", "tzd_rawmemchr" },
  /* tempnam: the same (a host malloc block).  strtok: the host's keeps
     its position as a raw pointer.  */
  { "tempnam", "tzd_tempnam" }, { "strtok", "tzd_strtok" },
  /* <wchar.h>: the host's are not checked (Juliet CWE135).  */
  { "wcslen", "tzd_wcslen" }, { "wcscpy", "tzd_wcscpy" },
  { "wcsncpy", "tzd_wcsncpy" }, { "wcscat", "tzd_wcscat" },
  { "wcsncat", "tzd_wcsncat" }, { "wmemcpy", "tzd_wmemcpy" },
  { "wmemmove", "tzd_wmemmove" }, { "wmemset", "tzd_wmemset" },
  { "strncasecmp", "tzd_strncasecmp" },
  { "malloc", "tzd_malloc" }, { "calloc", "tzd_calloc" },
  { "realloc", "tzd_realloc" }, { "reallocarray", "tzd_reallocarray" },
  { "free", "tzd_free" },
  /* Host qsort would move elements without the bounds of the pointers in
     them (task #1 (b)); libc2d's moves them and needs no ABI thunk.  */
  { "qsort", "tzd_qsort" }, { "qsort_r", "tzd_qsort_r" },
  /* CPU sets of sched_getaffinity (gnulib nproc): bytes for N CPUs.  */
  { "__sched_cpualloc", "tzd___sched_cpualloc" },
  { "__sched_cpufree", "tzd___sched_cpufree" },
  /* ctype (glibc's table macros are off: __NO_CTYPE) and conversions,
     written in Managed C (libc2d/tzd/, task #43).  */
  { "isalnum", "tzd_isalnum" }, { "isalpha", "tzd_isalpha" },
  { "iscntrl", "tzd_iscntrl" }, { "isdigit", "tzd_isdigit" },
  { "isgraph", "tzd_isgraph" }, { "islower", "tzd_islower" },
  { "isprint", "tzd_isprint" }, { "ispunct", "tzd_ispunct" },
  { "isspace", "tzd_isspace" }, { "isupper", "tzd_isupper" },
  { "isxdigit", "tzd_isxdigit" }, { "isblank", "tzd_isblank" },
  { "tolower", "tzd_tolower" }, { "toupper", "tzd_toupper" },
  { "abs", "tzd_abs" }, { "labs", "tzd_labs" },
  { "atoi", "tzd_atoi" }, { "atol", "tzd_atol" },
  { "strtol", "tzd_strtol" }, { "strtoul", "tzd_strtoul" },
  /* printf family: one Managed C formatter; sprintf stops at the
     destination's bounds, %n traps (task #43).  */
  { "sprintf", "tzd_sprintf" }, { "vsprintf", "tzd_vsprintf" },
  { "snprintf", "tzd_snprintf" }, { "vsnprintf", "tzd_vsnprintf" },
  { "fprintf", "tzd_fprintf" }, { "vfprintf", "tzd_vfprintf" },
  { "asprintf", "tzd_asprintf" }, { "vasprintf", "tzd_vasprintf" },
  { "dprintf", "tzd_dprintf" }, { "vdprintf", "tzd_vdprintf" },
  { "printf", "tzd_printf_stdio" }, { "vprintf", "tzd_vprintf_stdio" },
};

/* TYPE with every raw data pointer made managed, also inside function
   pointer types (a host comparator `int (*)(const void *, const void *)`
   becomes one taking managed pointers, i.e. a Managed C function).  */
static tree
libd_managed_type (tree t)
{
  if (raw_data_ptr_p (t))
    return tzd_mrec_type (TREE_TYPE (t));
  if (TREE_CODE (t) == POINTER_TYPE
      && TREE_CODE (TREE_TYPE (t)) == FUNCTION_TYPE
      && prototype_p (TREE_TYPE (t)))
    {
      tree ft = TREE_TYPE (t);
      vec<tree> args = vNULL;
      for (tree a = TYPE_ARG_TYPES (ft);
	   a && TREE_VALUE (a) != void_type_node; a = TREE_CHAIN (a))
	args.safe_push (libd_managed_type (TREE_VALUE (a)));
      tree nt = build_function_type_array (libd_managed_type (TREE_TYPE (ft)),
					   args.length (), args.address ());
      args.release ();
      return build_qualified_type (build_pointer_type (nt), TYPE_QUALS (t));
    }
  return t;
}

/* The libc2d replacement for host function FNDECL, or NULL_TREE.  Its type is
   the host prototype with every raw data pointer made managed.  */
static tree
libd_redirect (tree fndecl)
{
  if (!DECL_NAME (fndecl) || !prototype_p (TREE_TYPE (fndecl)))
    return NULL_TREE;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (fndecl));
  /* GCC's __builtin_memcpy & co. go to the same checked versions (an
     unchecked builtin copy would bypass the bounds; kernel, zstd).  */
  bool gcc_builtin = fndecl_built_in_p (fndecl, BUILT_IN_NORMAL)
		     && strncmp (name, "__builtin_", 10) == 0;
  if (gcc_builtin)
    name += 10;
  else if (!DECL_IN_SYSTEM_HEADER (fndecl)
	   /* Kernel: memcpy & co. are written in assembly (native); calls
	      go to the checked versions in kc2d.  */
	   && !(flag_tzd_kernel && native_name_p (DECL_NAME (fndecl))))
    return NULL_TREE;
  const char *dname = NULL;
  for (size_t i = 0; i < ARRAY_SIZE (libd_redirect_names); i++)
    if (strcmp (name, libd_redirect_names[i][0]) == 0)
      dname = libd_redirect_names[i][1];
  if (dname == NULL)
    return NULL_TREE;
  for (unsigned i = 0; libd_redirect_cache
		       && i + 1 < libd_redirect_cache->length (); i += 2)
    if ((*libd_redirect_cache)[i] == fndecl)
      return (*libd_redirect_cache)[i + 1];
  tree ft = TREE_TYPE (fndecl);
  auto managed = [] (tree t) { return libd_managed_type (t); };
  vec<tree> args = vNULL;
  for (tree a = TYPE_ARG_TYPES (ft); a && TREE_VALUE (a) != void_type_node;
       a = TREE_CHAIN (a))
    args.safe_push (managed (TREE_VALUE (a)));
  tree nt = (stdarg_p (ft)
	     ? build_varargs_function_type_array (managed (TREE_TYPE (ft)),
						  args.length (),
						  args.address ())
	     : build_function_type_array (managed (TREE_TYPE (ft)),
					  args.length (), args.address ()));
  args.release ();
  tree d = build_decl (DECL_SOURCE_LOCATION (fndecl), FUNCTION_DECL,
		       get_identifier (dname), nt);
  DECL_EXTERNAL (d) = 1;
  TREE_PUBLIC (d) = 1;
  DECL_ARTIFICIAL (d) = 1;
  TREE_NOTHROW (d) = 1;
  vec_safe_push (libd_redirect_cache, fndecl);
  vec_safe_push (libd_redirect_cache, d);
  return d;
}

/* Host functions that write or read through (buffer, length) arguments.
   Before such a call the buffer's managed bounds must cover the length
   (times the element size argument, if any), so read()/fgets()/... cannot
   overflow inside the host (task #14, provisional).  Indexes are 0-based
   argument positions; MUL is -1 when there is none.  */
struct host_buf_rule { const char *name; int buf, len, mul; };
static const host_buf_rule host_buf_rules[] = {
  { "read", 1, 2, -1 }, { "write", 1, 2, -1 }, { "pread", 1, 2, -1 },
  { "pwrite", 1, 2, -1 }, { "recv", 1, 2, -1 }, { "send", 1, 2, -1 },
  { "recvfrom", 1, 2, -1 }, { "sendto", 1, 2, -1 },
  { "fread", 0, 1, 2 }, { "fwrite", 0, 1, 2 },
  { "fgets", 0, 1, -1 }, { "snprintf", 0, 1, -1 }, { "vsnprintf", 0, 1, -1 },
  { "fgets_unlocked", 0, 1, -1 },
  { "inet_ntop", 2, 3, -1 },
  { "getmntent_r", 2, 3, -1 },
  { "strftime", 0, 1, -1 }, { "getcwd", 0, 1, -1 },
  { "gethostname", 0, 1, -1 }, { "readlink", 1, 2, -1 },
  { "memccpy", 0, 3, -1 }, { "strxfrm", 0, 2, -1 },
};

static const host_buf_rule *
host_buf_rule_for (tree fndecl)
{
  if (!fndecl || !DECL_IN_SYSTEM_HEADER (fndecl) || !DECL_NAME (fndecl))
    return NULL;
  const char *name = IDENTIFIER_POINTER (DECL_NAME (fndecl));
  for (size_t i = 0; i < ARRAY_SIZE (host_buf_rules); i++)
    if (strcmp (name, host_buf_rules[i].name) == 0)
      return &host_buf_rules[i];
  return NULL;
}

/* An argument for a transparent union parameter of type UT: a union
   object whose member of A's type holds A (GCC's transparent_union).  */
static ex
transparent_union_arg (ex a, tree ut, location_t loc)
{
  ex v = rvalue (a, loc);
  if (bad (v))
    return v;
  tree f;
  for (f = TYPE_FIELDS (ut); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      tree ft = TREE_TYPE (f);
      if (TYPE_MAIN_VARIANT (ft) == TYPE_MAIN_VARIANT (v.type)
	  || (TZD_MPTR_P (ft) && TZD_MPTR_P (v.type)
	      && TYPE_MAIN_VARIANT (TZD_MPTR_TARGET (ft))
		 == TYPE_MAIN_VARIANT (TZD_MPTR_TARGET (v.type)))
	  || (TZD_MPTR_P (ft) && (null_const_p (v) || null_mptr_p (v)))
	  /* A host union (glibc's __SOCKADDR_ARG) has raw pointer members:
	     a managed pointer to the same type is passed as its address.  */
	  || (POINTER_TYPE_P (ft) && !TZD_MPTR_P (ft) && TZD_MPTR_P (v.type)
	      && TYPE_MAIN_VARIANT (TREE_TYPE (ft))
		 == TYPE_MAIN_VARIANT (TZD_MPTR_TARGET (v.type)))
	  || (POINTER_TYPE_P (ft) && !TZD_MPTR_P (ft)
	      && (null_const_p (v) || null_mptr_p (v))))
	break;
    }
  if (!f)
    {
      error_at (loc, "argument does not match any member of the "
		"transparent union %qT", ut);
      return err ();
    }
  ex mv = convert_to (v, TREE_TYPE (f), loc);
  if (bad (mv))
    return mv;
  tree var = build_decl (loc, VAR_DECL, NULL_TREE, ut);
  DECL_ARTIFICIAL (var) = 1;
  DECL_CONTEXT (var) = current_function_decl;
  TREE_ADDRESSABLE (var) = 1;
  /* Wiped with the scope of the function being parsed; not inside a thunk
     (native_thunk builds its own body).  */
  if (cur_fn && cur_fn->decl == current_function_decl)
    cl_wipe_add (var);
  tree stmts = alloc_stmt_list ();
  append_to_statement_list_force (build2 (INIT_EXPR, ut, var,
					  build_constructor (ut, NULL)),
				  &stmts);
  tree ref = build3 (COMPONENT_REF, TREE_TYPE (f), var, f, NULL_TREE);
  if (TZD_MSLOT_P (TREE_TYPE (f)))
    append_to_statement_list_force (slot_store (ref, value (mv), loc), &stmts);
  else
    append_to_statement_list_force (build2 (MODIFY_EXPR, TREE_TYPE (f), ref,
					    value (mv)), &stmts);
  tree te = build4 (TARGET_EXPR, ut, var, stmts, NULL_TREE, NULL_TREE);
  TREE_SIDE_EFFECTS (te) = 1;
  SET_EXPR_LOCATION (te, loc);
  return mk (te, ut);
}

static ex
call (ex fn, location_t loc)
{
  /* Arguments.  */
  vec<ex> args = vNULL;
  vec<location_t> alocs = vNULL;
  next ();   /* ( */
  if (!is_p (")"))
    for (;;)
      {
	alocs.safe_push (loc0 ());
	args.safe_push (parse_assign ());
	if (!accept (","))
	  break;
      }
  expect (")");
  if (bad (fn))
    {
      args.release ();
      alocs.release ();
      return err ();
    }

  /* `#pragma tzd refuse_call F N`: the call does not happen.  */
  if (TREE_CODE (fn.type) == FUNCTION_TYPE && TREE_CODE (fn.t) == FUNCTION_DECL
      && DECL_NAME (fn.t))
    {
      long refused;
      if (tzd_refuse_call_p (IDENTIFIER_POINTER (DECL_NAME (fn.t)), &refused))
	{
	  tree r = build_int_cst (integer_type_node, refused);
	  for (unsigned i = args.length (); i-- > 0;)
	    {
	      ex a = rvalue (args[i], loc);
	      if (bad (a))
		continue;
	      tree v = TZD_MPTR_P (a.type) ? whole (a) : a.t;
	      r = build2 (COMPOUND_EXPR, integer_type_node,
			  build1 (NOP_EXPR, void_type_node, v), r);
	    }
	  args.release ();
	  alocs.release ();
	  ex res = mk (r, integer_type_node);
	  TREE_SIDE_EFFECTS (r) = 1;
	  return res;
	}
    }

  tree fntype, fnaddr;
  tree fndecl = NULL_TREE;
  bool redirected = false;
  if (TREE_CODE (fn.type) == FUNCTION_TYPE)
    {
      fntype = fn.type;
      if (TREE_CODE (fn.t) == FUNCTION_DECL)
	{
	  fndecl = fn.t;
	  if (tree r = libd_redirect (fndecl))
	    {
	      fndecl = r;
	      fntype = TREE_TYPE (r);
	      redirected = true;
	    }
	  TREE_USED (fndecl) = 1;
	  fnaddr = build_fold_addr_expr_loc (loc, fndecl);
	}
      else
	fnaddr = fn.t;
    }
  else
    {
      fn = rvalue (fn, loc);
      if (!fnptr_p (fn.type))
	{
	  error_at (loc, "called object is not a function");
	  args.release ();
	  alocs.release ();
	  return err ();
	}
      fntype = TREE_TYPE (fn.type);
      fnaddr = fn.t;
    }
  bool builtin = fndecl && fndecl_built_in_p (fndecl);
  tree arg_vars = NULL_TREE, arg_inits = NULL_TREE;  /* record temporaries */
  /* Host-side callee (declared in a system header, or a function type with
     raw pointer parameters): variadic pointers are passed raw.  */
  /* A libc2d redirect is Managed C: its varargs pointers are records.  */
  bool native_fn = fndecl && !redirected
		   ? DECL_IN_SYSTEM_HEADER (fndecl)
		     && !managed_override_name_p (DECL_NAME (fndecl))
		   : false;
  if (!native_fn)
    for (tree t = TYPE_ARG_TYPES (fntype); t; t = TREE_CHAIN (t))
      if (raw_data_ptr_p (TREE_VALUE (t)))
	native_fn = true;

  unsigned n = args.length ();
  tree *argv = XALLOCAVEC (tree, n + 1);
  tree pt = TYPE_ARG_TYPES (fntype);
  bool ok = true;

  /* C11 atomics / __sync builtins (task #61): these are type-generic, so the
     normal prototype matching does not fit.  Bounds-check and raw-convert
     each managed pointer argument (the atomic touches sizeof(*p) bytes
     there, like __builtin_*_overflow's output pointer), then let GCC resolve
     the overload to the correctly typed builtin.  (OpenSSL reaches them via
     <stdatomic.h> under #pragma tzd atomics, and via refcount.h's GCC path.)  */
  if (builtin && DECL_NAME (fndecl))
    {
      const char *bn = IDENTIFIER_POINTER (DECL_NAME (fndecl));
      if (startswith (bn, "__atomic_") || startswith (bn, "__sync_"))
	{
	  vec<tree, va_gc> *pv = NULL;
	  for (unsigned i = 0; i < n; i++)
	    {
	      ex a = rvalue (args[i], alocs[i]);
	      if (bad (a))
		return err ();
	      if (TZD_MPTR_P (a.type))
		{
		  tree target = TZD_MPTR_TARGET (a.type);
		  parts (a);
		  tree chk = build_check (a.pa, a.pb, a.ps,
					  TYPE_SIZE_UNIT (target), alocs[i]);
		  tree raw = fold_convert (build_pointer_type (target), a.pa);
		  vec_safe_push (pv, chk ? build2 (COMPOUND_EXPR, TREE_TYPE (raw),
						   chk, raw)
					 : raw);
		}
	      else
		vec_safe_push (pv, value (a));
	    }
	  tree res = resolve_overloaded_builtin (loc, fndecl, pv);
	  if (res == error_mark_node)
	    return err ();
	  if (res)
	    return mk (res, TREE_TYPE (res));
	  /* NULL: not a type-generic builtin (e.g. __atomic_thread_fence,
	     __atomic_signal_fence, which take only a memory order); fall
	     through to the normal call path.  The pv built above is discarded
	     and emits nothing.  */
	}
    }
  tree post = NULL_TREE;   /* copy-back after the call (tzd_pp_out) */
  tree pre = NULL_TREE;    /* buffer-length check before the call */
  if (const host_buf_rule *rule = host_buf_rule_for (fndecl))
    if ((unsigned) MAX (rule->buf, MAX (rule->len, rule->mul)) < n)
      {
	ex b = rvalue (args[rule->buf], alocs[rule->buf]);
	ex l = rvalue (args[rule->len], alocs[rule->len]);
	ex m = rule->mul >= 0 ? rvalue (args[rule->mul], alocs[rule->mul])
	       : mk (size_one_node, size_type_node);
	if (!bad (b) && !bad (l) && !bad (m) && TZD_MPTR_P (b.type)
	    && INTEGRAL_TYPE_P (l.type) && INTEGRAL_TYPE_P (m.type))
	  {
	    parts (b);
	    b.pa = save_expr (b.pa);
	    b.pb = save_expr (b.pb);
	    b.ps = save_expr (b.ps);
	    l.t = save_expr (l.t);
	    m.t = save_expr (m.t);
	    tree len = fold_build2_loc (loc, MULT_EXPR, size_type_node,
					fold_convert (size_type_node, l.t),
					fold_convert (size_type_node, m.t));
	    /* A zero length touches nothing (and may come with NULL).  */
	    tree chk = build_check (b.pa, b.pb, b.ps, len, loc);
	    if (chk)
	      pre = build3 (COND_EXPR, void_type_node,
			    fold_build2_loc (loc, NE_EXPR, integer_type_node,
					     len, size_zero_node),
			    chk, build_empty_stmt (loc));
	    args[rule->buf] = b;
	    args[rule->len] = l;
	    if (rule->mul >= 0)
	      args[rule->mul] = m;
	  }
      }
  /* Bounds rule for a raw pointer result of a host function (task #1).  */
  const host_ptr_rule *prule = NULL;
  tree rargb = NULL_TREE, rargs = NULL_TREE, rargn = NULL_TREE;
  if (fndecl && DECL_IN_SYSTEM_HEADER (fndecl) && DECL_NAME (fndecl)
      && raw_data_ptr_p (TREE_TYPE (fntype)))
    {
      prule = host_ptr_rule_for (IDENTIFIER_POINTER (DECL_NAME (fndecl)));
      if (!prule)
	{
	  error_at (loc, "no bounds rule for the pointer returned by host "
		    "function %qD; add one to host_ptr_rules (task #1)",
		    fndecl);
	  ok = false;
	}
      else if ((prule->kind == 'g' || prule->kind == 'c' || prule->kind == 'l'
		   || prule->kind == 'm')
	       && (unsigned) prule->a < n)
	{
	  ex b = rvalue (args[prule->a], alocs[prule->a]);
	  if (bad (b))
	    ok = false;
	  else if ((prule->kind == 'g' || prule->kind == 'c')
		   && TZD_MPTR_P (b.type))
	    {
	      parts (b);
	      b.pa = save_expr (b.pa);
	      b.pb = save_expr (b.pb);
	      b.ps = save_expr (b.ps);
	      rargb = b.pb;
	      rargs = b.ps;
	      args[prule->a] = b;
	    }
	  else if (prule->kind == 'c' && INTEGRAL_TYPE_P (b.type)
		   && TREE_CODE (b.t) == INTEGER_CST && integer_zerop (b.t))
	    {
	      /* A null constant written as 0 (getcwd (0, 0)): no buffer, so
		 the result is always the host string.  */
	      rargb = null_pointer_node;
	      rargs = size_zero_node;
	    }
	  else if ((prule->kind == 'l' || prule->kind == 'm')
		   && INTEGRAL_TYPE_P (b.type))
	    {
	      b.t = save_expr (b.t);
	      rargn = b.t;
	      args[prule->a] = b;
	    }
	  else
	    {
	      error_at (loc, "host pointer rule for %qD does not fit its "
			"arguments", fndecl);
	      ok = false;
	    }
	}
      else if (prule->kind == 'g' || prule->kind == 'c' || prule->kind == 'l'
		   || prule->kind == 'm')
	{
	  error_at (loc, "host pointer rule for %qD does not fit its "
		    "arguments", fndecl);
	  ok = false;
	}
    }
  /* Write groups (task #79): a library function writes through its
     non-const pointer parameters with the caller's permission (scanf
     also through its variadic ones), so the caller's name is tested on
     them here.  */
  /* libc2d's entry points are named tzd_* (include/string.h:
     `memcpy (...) __asm__ ("tzd_memcpy")`); testing at a call only adds
     checks, so the name is enough to tell.  */
  auto libd_entry_p = [] (tree d)
    {
      tree an = DECL_ASSEMBLER_NAME_SET_P (d) ? DECL_ASSEMBLER_NAME (d)
		: DECL_NAME (d);
      if (!an)
	return false;
      const char *nm = IDENTIFIER_POINTER (an);
      if (*nm == '*')
	nm++;
      return strncmp (nm, "tzd_", 4) == 0;
    };
  bool grp_deleg = grp_active_p () && fndecl
		   && (builtin || redirected || DECL_IN_SYSTEM_HEADER (fndecl)
		       || libd_entry_p (fndecl));
  bool grp_scanf = grp_deleg && DECL_NAME (fndecl)
		   && strstr (IDENTIFIER_POINTER (DECL_NAME (fndecl)), "scanf");
  tree grp_pre = NULL_TREE;
  for (unsigned i = 0; i < n; i++)
    {
      ex a = args[i];
      tree pty_i = pt && TREE_VALUE (pt) != void_type_node
		   ? TREE_VALUE (pt) : NULL_TREE;
      if (pt && TREE_VALUE (pt) != void_type_node
	  && raw_data_ptr_p (TREE_VALUE (pt))
	  && raw_data_ptr_p (TREE_TYPE (TREE_VALUE (pt))))
	{
	  /* Host T ** parameter: marshal a managed array of managed
	     pointers (getopt's argv, strtol's endptr; libc2d boundary.c).  */
	  ex r = rvalue (a, alocs[i]);
	  if (!bad (r) && TZD_MPTR_P (r.type)
	      && TZD_MPTR_P (TZD_MPTR_TARGET (r.type)))
	    {
	      tree vp = tzd_mrec_type (void_type_node);
	      tree vpp = build_pointer_type (ptr_type_node);
	      if (tzd_pp_in_decl == NULL_TREE)
		{
		  tzd_pp_in_decl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
					     get_identifier ("tzd_pp_in"),
					     build_function_type_list
					       (vpp, vp, NULL_TREE));
		  tzd_pp_out_decl = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
					      get_identifier ("tzd_pp_out"),
					      build_function_type_list
						(void_type_node, vp, vpp,
						 NULL_TREE));
		  tree ds[2] = { tzd_pp_in_decl, tzd_pp_out_decl };
		  for (int k = 0; k < 2; k++)
		    {
		      DECL_EXTERNAL (ds[k]) = 1;
		      TREE_PUBLIC (ds[k]) = 1;
		      DECL_ARTIFICIAL (ds[k]) = 1;
		      TREE_NOTHROW (ds[k]) = 1;
		    }
		}
	      tree pv = save_expr (value (convert_to (r, vp, alocs[i], true)));
	      tree raw = save_expr (build_call_expr_loc (alocs[i], tzd_pp_in_decl,
							 1, pv));
	      tree out = build_call_expr_loc (alocs[i], tzd_pp_out_decl, 2, pv,
					      raw);
	      post = post ? build2 (COMPOUND_EXPR, void_type_node, post, out)
		     : out;
	      argv[i] = fold_convert (TREE_VALUE (pt), raw);
	      pt = TREE_CHAIN (pt);
	      continue;
	    }
	  a = r;
	}
      if (pt && TREE_VALUE (pt) != void_type_node)
	{
	  tree ptype = TREE_VALUE (pt);
	  if (builtin && TREE_CODE (ptype) == POINTER_TYPE
	      && TREE_CODE (a.type) == ARRAY_TYPE && a.t)
	    /* Raw pointer parameter of a GCC builtin (va_list etc.).  */
	    a = mk (fold_convert (ptype, array_first_addr (a.t, alocs[i])),
		    ptype);
	  else if (TREE_CODE (ptype) == UNION_TYPE
		   && TYPE_TRANSPARENT_AGGR (TYPE_MAIN_VARIANT (ptype))
		   && !bad (a)
		   && TYPE_MAIN_VARIANT (rvalue (a, alocs[i]).type)
		      != TYPE_MAIN_VARIANT (ptype))
	    a = transparent_union_arg (a, ptype, alocs[i]);
	  else if (!builtin && TREE_CODE (ptype) == POINTER_TYPE)
	    a = handout_arg (a, ptype, alocs[i]);
	  else
	    a = convert_to (a, ptype, alocs[i]);
	  pt = TREE_CHAIN (pt);
	}
      else if (pt && TREE_VALUE (pt) == void_type_node)
	{
	  error_at (loc, "too many arguments to function");
	  ok = false;
	  break;
	}
      else
	{
	  /* Variadic part or unprototyped: default promotions.  */
	  a = rvalue (a, alocs[i]);
	  if (!bad (a) && builtin && i == 2 && TZD_MPTR_P (a.type)
	      && (fndecl_built_in_p (fndecl, BUILT_IN_ADD_OVERFLOW)
		  || fndecl_built_in_p (fndecl, BUILT_IN_SUB_OVERFLOW)
		  || fndecl_built_in_p (fndecl, BUILT_IN_MUL_OVERFLOW)))
	    {
	      /* __builtin_*_overflow (a, b, d): GCC writes *d -- check that
		 d reaches sizeof *d, then pass its raw address.  */
	      tree target = TZD_MPTR_TARGET (a.type);
	      parts (a);
	      tree chk = build_check (a.pa, a.pb, a.ps, TYPE_SIZE_UNIT (target),
				      alocs[i]);
	      tree raw = fold_convert (build_pointer_type (target), a.pa);
	      a = mk (chk ? build2 (COMPOUND_EXPR, TREE_TYPE (raw), chk, raw)
		      : raw, TREE_TYPE (raw));
	    }
	  else if (!bad (a) && TYPE_MAIN_VARIANT (a.type) == float_type_node)
	    a = convert_to (a, double_type_node, alocs[i]);
	  else if (!bad (a) && native_fn && TZD_MPTR_P (a.type))
	    /* Host function (printf, execl, sscanf, ...): raw pointer().  */
	    a = handout_arg (a, build_pointer_type (TZD_MPTR_TARGET (a.type)),
			     alocs[i]);
	  else
	    a = promote (a, alocs[i]);
	}
      if (bad (a))
	ok = false;
      argv[i] = value (a);
      if (grp_deleg && ok && (pty_i || grp_scanf))
	{
	  tree at = pty_i ? pty_i : a.type;
	  bool wr = false;
	  if (TZD_MPTR_P (at) && TZD_MPTR_P (a.type))
	    wr = !TYPE_READONLY (TZD_MPTR_TARGET (at));
	  else if (TREE_CODE (at) == POINTER_TYPE
		   && TREE_CODE (TREE_TYPE (at)) != FUNCTION_TYPE
		   && POINTER_TYPE_P (TREE_TYPE (argv[i])))
	    wr = !TYPE_READONLY (TREE_TYPE (at));
	  if (wr)
	    {
	      argv[i] = save_expr (argv[i]);
	      tree addr = TZD_MPTR_P (a.type)
			  ? build3 (COMPONENT_REF,
				    TREE_TYPE (mp_field (a.type, 0)), argv[i],
				    mp_field (a.type, 0), NULL_TREE)
			  : argv[i];
	      tree g = grp_guard_addr (addr, alocs[i]);
	      grp_pre = grp_pre ? build2 (COMPOUND_EXPR, void_type_node,
					  grp_pre, g) : g;
	    }
	}
      if (!bad (a) && !builtin && TZD_MPTR_P (a.type) && current_function_decl
	  && !DECL_P (argv[i]) && !TREE_CONSTANT (argv[i])
	  && TREE_CODE (argv[i]) != TARGET_EXPR
	  && AGGREGATE_TYPE_P (TREE_TYPE (argv[i])))
	{
	  /* A pointer record built for this call: a temporary that lives
	     only until the call returns (declared in a BIND_EXPR around
	     the call below, which the gimplifier clobbers at its end), so
	     the stack slots of the temporaries are shared.  Left to the
	     gimplifier, every call site had its own 24 bytes for the whole
	     function (x64_sys_call: 11 KB of them).  */
	  tree tmp = build_decl (loc, VAR_DECL, NULL_TREE,
				 TREE_TYPE (argv[i]));
	  DECL_ARTIFICIAL (tmp) = 1;
	  DECL_IGNORED_P (tmp) = 1;
	  DECL_CONTEXT (tmp) = current_function_decl;
	  tree init = build2 (INIT_EXPR, void_type_node, tmp, argv[i]);
	  TREE_SIDE_EFFECTS (init) = 1;
	  arg_inits = arg_inits
		      ? build2 (COMPOUND_EXPR, void_type_node, arg_inits, init)
		      : init;
	  DECL_CHAIN (tmp) = arg_vars;
	  arg_vars = tmp;
	  argv[i] = tmp;
	}
    }
  if (ok && pt && TREE_VALUE (pt) != void_type_node)
    {
      error_at (loc, "too few arguments to function");
      ok = false;
    }
  if (ok && builtin && fndecl_built_in_p (fndecl, BUILT_IN_VA_START)
      && n == 2)
    argv[1] = integer_zero_node;
  args.release ();
  alocs.release ();
  if (!ok)
    return err ();

  tree rt = TREE_TYPE (fntype);
  tree sv_res = NULL_TREE;
  if (sv_fntype_p (fntype))
    {
      /* Lowered call (sv): struct arguments as pointers to bounds-copied
	 temporaries, the struct result through a hidden first argument.  */
      tree lt = sv_lower_type (fntype);
      fnaddr = fold_convert (build_pointer_type (lt), fnaddr);
      tree *nargv = XALLOCAVEC (tree, n + 2);
      unsigned k = 0;
      if (sv_type_p (rt))
	{
	  tree te = sv_temp (rt, NULL_TREE, &sv_res, loc);
	  nargv[k++] = build_fold_addr_expr_loc (loc, te);
	}
      tree a = TYPE_ARG_TYPES (fntype);
      for (unsigned i = 0; i < n; i++)
	{
	  tree at = a && TREE_VALUE (a) != void_type_node ? TREE_VALUE (a)
							   : NULL_TREE;
	  if (at && sv_type_p (at))
	    {
	      if (!memory_ref_p (argv[i]))
		{
		  sorry_at (loc, "tzd: passing a struct that contains pointers "
			    "from a value that is not an object (task #46)");
		  return err ();
		}
	      tree slot;
	      tree te = sv_temp (at, argv[i], &slot, loc);
	      nargv[k++] = build_fold_addr_expr_loc (loc, te);
	    }
	  else
	    nargv[k++] = argv[i];
	  if (a)
	    a = TREE_CHAIN (a);
	}
      argv = nargv;
      n = k;
    }
  /* alloca (N): N bytes of the caller's frame (Juliet's stack buffers).  */
  bool is_alloca = fndecl && n >= 1
		   && (fndecl_built_in_p (fndecl, BUILT_IN_ALLOCA)
		       || fndecl_built_in_p (fndecl, BUILT_IN_ALLOCA_WITH_ALIGN));
  if (is_alloca)
    argv[0] = save_expr (argv[0]);
  tree c = build_call_array_loc (loc, sv_res ? void_type_node : rt, fnaddr,
				 n, argv);
  if (is_alloca)
    {
      tree p = save_expr (c);
      return mk_parts (tzd_mptr_type (void_type_node), p, p,
		       fold_convert (size_type_node, argv[0]));
    }
  if (arg_vars)
    {
      c = build2 (COMPOUND_EXPR, TREE_TYPE (c), arg_inits, c);
      c = build3 (BIND_EXPR, TREE_TYPE (c), arg_vars, c, NULL_TREE);
      TREE_SIDE_EFFECTS (c) = 1;
    }
  if (builtin && !sv_res)
    /* GCC builtins fold as in C: __builtin_constant_p (5) is 1, so
       htons (ETH_P_IP) is a constant usable as a case label.  */
    if (tree f = fold_call_expr (loc, c, false))
      c = f;
  if (grp_pre)
    {
      c = build2 (COMPOUND_EXPR, TREE_TYPE (c), grp_pre, c);
      TREE_SIDE_EFFECTS (c) = 1;
    }
  if (sv_res)
    {
      /* [pre,] the call[, post], then its result object (aggregate_copy
	 and memory_ref_p look through the COMPOUND_EXPR).  */
      if (pre)
	c = build2 (COMPOUND_EXPR, void_type_node, pre, c);
      if (post)
	c = build2 (COMPOUND_EXPR, void_type_node, c, post);
      return mk (build2 (COMPOUND_EXPR, rt, c, sv_res), rt);
    }
  if (pre)
    c = build2 (COMPOUND_EXPR, rt, pre, c);
  if (post)
    {
      if (VOID_TYPE_P (rt))
	c = build2 (COMPOUND_EXPR, rt, c, post);
      else
	{
	  c = save_expr (c);
	  c = build2 (COMPOUND_EXPR, rt, build2 (COMPOUND_EXPR, void_type_node,
						 c, post), c);
	}
    }
  if (prule)
    return host_rule_apply (c, rt, prule->kind, prule->a, prule->b,
			    prule->ekind, prule->ea, prule->eb, rargb, rargs,
			    rargn, loc);
  if (fndecl && raw_data_ptr_p (rt) && native_name_p (DECL_NAME (fndecl)))
    /* Raw pointer from a native (assembly) function.  */
    return from_native (c, tzd_mptr_type (TREE_TYPE (rt)), loc);
  return mk (c, rt);
}

static ex
incdec_store (ex e, bool inc, bool post, location_t loc);

/* ++/--: the write-group test (task #79) comes before the store.  */
static ex
incdec (ex e, bool inc, bool post, location_t loc)
{
  if (bad (e))
    return e;
  if (!e.lv)
    {
      error_at (loc, "lvalue required as increment/decrement operand");
      return err ();
    }
  tree guard = grp_store_guard (&e.t, loc);
  return grp_prepend (guard, incdec_store (e, inc, post, loc));
}

static ex
incdec_store (ex e, bool inc, bool post, location_t loc)
{
  tree l = stabilize_reference (e.t);
  if (TZD_MPTR_P (e.type))
    {
      tree target = TZD_MPTR_TARGET (e.type);
      if (!VOID_TYPE_P (target) && !COMPLETE_TYPE_P (target))
	{
	  error_at (loc, "pointer arithmetic on pointer to incomplete type");
	  return err ();
	}
      if (TZD_MSLOT_P (TREE_TYPE (l)))
	{
	  /* Slot in memory: load, step, store (shadow entry included).  */
	  ex cur = rvalue (mk (l, e.type, true), loc);
	  parts (cur);
	  cur.pa = save_expr (cur.pa);
	  cur.pb = save_expr (cur.pb);
	  cur.ps = save_expr (cur.ps);
	  tree step = fold_convert (sizetype, arith_size (target));
	  if (!inc)
	    step = fold_build1_loc (loc, NEGATE_EXPR, sizetype, step);
	  ex nv = mk_parts (e.type,
			    fold_build_pointer_plus_loc (loc, cur.pa, step),
			    cur.pb, cur.ps);
	  tree v = save_expr (whole (nv));
	  tree st = slot_store (l, v, loc);
	  tree res = post ? whole (cur) : v;
	  tree r = build2 (COMPOUND_EXPR, TREE_TYPE (res), st, res);
	  TREE_SIDE_EFFECTS (r) = 1;
	  return mk (r, e.type);
	}
      tree fa = build3 (COMPONENT_REF, mp_native (e.type), l,
			mp_field (e.type, 0), NULL_TREE);
      tree amount = fold_convert (sizetype, arith_size (target));
      enum tree_code code = post ? (inc ? POSTINCREMENT_EXPR
				    : POSTDECREMENT_EXPR)
			    : (inc ? PREINCREMENT_EXPR : PREDECREMENT_EXPR);
      tree a = build2 (code, TREE_TYPE (fa), fa, amount);
      TREE_SIDE_EFFECTS (a) = 1;
      ex le = mk (l, e.type, true);
      parts (le);
      return mk_parts (e.type, save_expr (a), le.pb, le.ps);
    }
  if (raw_data_ptr_p (e.type))
    {
      /* Raw pointer lvalue (system-header code, e.g. glibc's inline
	 getc: fp->_IO_read_ptr++).  */
      tree target = TREE_TYPE (e.type);
      if (!VOID_TYPE_P (target) && !COMPLETE_TYPE_P (target))
	{
	  error_at (loc, "pointer arithmetic on pointer to incomplete type");
	  return err ();
	}
      /* In user code, a host variable or field that has a bounds rule
	 (getopt's optarg: `optarg++`, `*p = optarg++`; task #71): the
	 pointer is read through its rule, stepped, and the new address is
	 stored back raw; the result carries the rule's bounds.  The step
	 may leave the bounds (optarg--): arithmetic is not an access, and
	 the next read of the variable applies the rule to the new address.  */
      if (!native_region_p ())
	{
	  char desc[96];
	  tree src = l;
	  if (host_rule_lookup (src, desc, sizeof desc))
	    {
	      ex cur = rvalue (mk (l, e.type, true), loc);
	      if (!bad (cur) && TZD_MPTR_P (cur.type))
		{
		  parts (cur);
		  cur.pa = save_expr (cur.pa);
		  cur.pb = save_expr (cur.pb);
		  cur.ps = save_expr (cur.ps);
		  tree step = fold_convert (sizetype, arith_size (target));
		  if (!inc)
		    step = fold_build1_loc (loc, NEGATE_EXPR, sizetype, step);
		  tree na = save_expr (fold_build_pointer_plus_loc (loc, cur.pa,
								    step));
		  tree st = build2 (MODIFY_EXPR, e.type, l,
				    fold_convert (e.type, na));
		  TREE_SIDE_EFFECTS (st) = 1;
		  tree ra = post ? cur.pa : na;
		  tree res = build2 (COMPOUND_EXPR, TREE_TYPE (ra), st, ra);
		  TREE_SIDE_EFFECTS (res) = 1;
		  return mk_parts (cur.type, res, cur.pb, cur.ps);
		}
	    }
	}
      enum tree_code code = post ? (inc ? POSTINCREMENT_EXPR
				    : POSTDECREMENT_EXPR)
			    : (inc ? PREINCREMENT_EXPR : PREDECREMENT_EXPR);
      tree a = build2 (code, e.type, l,
		       fold_convert (sizetype, arith_size (target)));
      TREE_SIDE_EFFECTS (a) = 1;
      return mk (a, e.type);
    }
  if (!arith_p (e.type))
    {
      error_at (loc, "wrong type argument to increment/decrement");
      return err ();
    }
  enum tree_code code = post ? (inc ? POSTINCREMENT_EXPR : POSTDECREMENT_EXPR)
			: (inc ? PREINCREMENT_EXPR : PREDECREMENT_EXPR);
  tree one = convert (TREE_TYPE (l), integer_one_node);
  tree r = build2 (code, TREE_TYPE (l), l, one);
  TREE_SIDE_EFFECTS (r) = 1;
  /* The store of ++/-- writes heap bytes (task #8); its read is not
     tested (the lenient side).  */
  if (tree mark = hp_store_mark (l, TREE_TYPE (l), loc))
    {
      r = build2 (COMPOUND_EXPR, TREE_TYPE (r), mark, r);
      TREE_SIDE_EFFECTS (r) = 1;
    }
  return mk (r, TYPE_MAIN_VARIANT (e.type));
}


static ex parse_postfix_ops (ex e);
static ex compound_literal (tree type, location_t loc);
static tree parse_initializer (tree type, bool is_static);
static tree complete_array_from_init (tree type, tree init);
static tree lower_slot_init (tree init, tree type, tree ref, bool is_static,
			     tree *stmts, location_t loc);

static ex
parse_postfix (void)
{
  return parse_postfix_ops (parse_primary ());
}

/* Postfix operators applied to E: calls, [i], ., ->, ++, --.  */
static ex
parse_postfix_ops (ex e)
{
  for (;;)
    {
      location_t loc = loc0 ();
      if (is_p ("("))
	e = call (e, loc);
      else if (accept ("["))
	{
	  ex idx = parse_expr ();
	  expect ("]");
	  ex a = rvalue (e, loc), i = rvalue (idx, loc);
	  if (bad (a) || bad (i))
	    e = err ();
	  else
	    e = deref (binop ("+", a, i, loc), loc);
	}
      else if (accept ("."))
	{
	  tree id = peek ().kind == TZD_IDENT ? get_identifier (peek ().text)
		    : NULL_TREE;
	  if (!id)
	    {
	      error_at (loc0 (), "expected member name");
	      return err ();
	    }
	  next ();
	  e = member (e, id, loc);
	}
      else if (accept ("->"))
	{
	  tree id = peek ().kind == TZD_IDENT ? get_identifier (peek ().text)
		    : NULL_TREE;
	  if (!id)
	    {
	      error_at (loc0 (), "expected member name");
	      return err ();
	    }
	  next ();
	  /* A named member directly in the struct, not a bit-field: check
	     only its bytes.  */
	  tree field = NULL_TREE, foff = NULL_TREE;
	  {
	    ex r = rvalue (e, loc);
	    if (!bad (r) && TZD_MPTR_P (r.type))
	      {
		tree st = TZD_MPTR_TARGET (r.type);
		if (RECORD_OR_UNION_TYPE_P (st) && COMPLETE_TYPE_P (st))
		  field = member_path (st, id, &foff);
	      }
	    e = r;
	  }
	  /* p->a.b.c: the bytes of the last member named (through named
	     struct or union members, read ahead): list_for_each_entry_safe
	     over pos->links.defer_sync from a list head reads only the
	     list_head.  */
	  if (field)
	    {
	      unsigned k = tpos;
	      while (k + 1 < toks.length ()
		     && toks[k].kind == TZD_PUNCT
		     && strcmp (toks[k].text, ".") == 0
		     && toks[k + 1].kind == TZD_IDENT
		     && RECORD_OR_UNION_TYPE_P (TREE_TYPE (field)))
		{
		  tree soff;
		  tree sub = member_path (TREE_TYPE (field),
					  get_identifier (toks[k + 1].text),
					  &soff);
		  if (!sub)
		    break;
		  foff = size_binop (PLUS_EXPR, foff, soff);
		  field = sub;
		  k += 2;
		}
	    }
	  e = member (deref (e, loc, field, foff), id, loc);
	}
      else if (accept ("++"))
	e = incdec (e, true, true, loc);
      else if (accept ("--"))
	e = incdec (e, false, true, loc);
      else
	return e;
    }
}

static ex parse_cast (void);

static tree
size_of_type (tree t, location_t loc)
{
  if (error_p (t))
    return error_mark_node;
  if (VOID_TYPE_P (t))
    return size_one_node;   /* GNU C: sizeof (void) == 1 */
  if (TREE_CODE (t) == FUNCTION_TYPE || !COMPLETE_TYPE_P (t))
    {
      error_at (loc, "invalid application of %<sizeof%> to an incomplete "
		"type");
      return error_mark_node;
    }
  return fold_convert (size_type_node, TYPE_SIZE_UNIT (t));
}

static ex
parse_unary (void)
{
  location_t loc = loc0 ();
  if (is_w ("__extension__"))
    {
      /* A no-op prefix on an expression (stdatomic.h's __extension__({...})
         used as a statement; it only suppresses pedantic warnings).  */
      next ();
      return parse_cast ();
    }
  if (accept ("++"))
    return incdec (parse_unary (), true, false, loc);
  if (accept ("--"))
    return incdec (parse_unary (), false, false, loc);
  if (accept ("*"))
    return deref (parse_cast (), loc);
  if (is_p ("&&") && peek (1).kind == TZD_IDENT && cur_fn)
    {
      /* &&label (GNU labels as values, the kernel's BPF interpreter
	 jump table): a code address like (void *) fn -- no bytes reachable,
	 reading through it traps.  */
      next ();
      tree l = get_label (get_identifier (peek ().text), loc);
      next ();
      TREE_USED (l) = 1;
      FORCED_LABEL (l) = 1;
      if (static_init_depth > 0)
	cur_fn->label_in_static = true;
      tree a = build1_loc (loc, ADDR_EXPR, ptr_type_node, l);
      TREE_CONSTANT (a) = 1;
      tree pt = tzd_mptr_type (void_type_node);
      return mk_parts (pt, fold_convert (mp_native (pt), a),
		       null_pointer_node, size_zero_node);
    }
  if (accept ("&"))
    return address_of (parse_cast (), loc);
  if (accept ("!"))
    {
      ex o = parse_cast ();
      bool ice = o.ice;
      ex r = mk (truth (o, loc, true), integer_type_node);
      r.ice = ice;
      return r;
    }
  if (accept ("-") || accept ("+") || accept ("~"))
    {
      char op = toks[tpos - 1].text[0];
      ex o = parse_cast ();
      bool ice = o.ice;
      ex e = promote (o, loc);
      if (bad (e))
	return e;
      e.ice = ice;
      if (!arith_p (e.type) || (op == '~' && !INTEGRAL_TYPE_P (e.type)))
	{
	  error_at (loc, "wrong type argument to unary %c", op);
	  return err ();
	}
      if (op == '+')
	return e;
      ex r = mk (fold_build1_loc (loc, op == '-' ? NEGATE_EXPR : BIT_NOT_EXPR,
				  e.type, e.t), e.type);
      r.ice = ice;
      return r;
    }
  if (is_w ("sizeof"))
    {
      next ();
      tree s;
      if (is_p ("(") && typename_start_p (1))
	{
	  next ();
	  tree t = parse_type_name ();
	  expect (")");
	  s = size_of_type (t, loc);
	}
      else
	{
	  unevaluated++;
	  ex e = parse_unary ();
	  unevaluated--;
	  s = bad (e) ? error_mark_node : size_of_type (e.type, loc);
	}
      if (error_p (s))
	return err ();
      ex r = mk (s, size_type_node);
      r.ice = true;
      return r;
    }
  if (is_w ("__alignof__") || is_w ("__alignof") || is_w ("_Alignof"))
    {
      /* As GCC: the type's alignment; for a variable or member its
	 declared alignment (aligned attribute included).  */
      next ();
      tree t, a = NULL_TREE;
      if (is_p ("(") && typename_start_p (1))
	{
	  next ();
	  t = parse_type_name ();
	  expect (")");
	}
      else
	{
	  unevaluated++;
	  ex e = parse_unary ();
	  unevaluated--;
	  if (bad (e))
	    return err ();
	  t = e.type;
	  tree d = e.t;
	  if (TREE_CODE (d) == COMPONENT_REF)
	    d = TREE_OPERAND (d, 1);
	  if (DECL_P (d) && TREE_CODE (d) != FUNCTION_DECL)
	    a = size_int (DECL_ALIGN_UNIT (d));
	}
      if (error_p (t) || TREE_CODE (t) == FUNCTION_TYPE || VOID_TYPE_P (t)
	  || (!a && !COMPLETE_TYPE_P (t)))
	{
	  /* A member's declared alignment needs no complete type: a
	     flexible array member (crypto.h's __alignof__ (tfm->__crt_ctx)),
	     as in GCC.  */
	  error_at (loc, "invalid application of %<__alignof__%> to an "
		    "incomplete type");
	  return err ();
	}
      if (!a)
	a = size_int (TYPE_ALIGN_UNIT (t));
      ex r = mk (fold_convert (size_type_node, a), size_type_node);
      r.ice = true;
      return r;
    }
  if (skip_gnu_noise (false))
    return parse_cast ();
  return parse_postfix ();
}

/* C99 compound literal (T){ ... }: an unnamed object initialized where the
   expression is evaluated, an lvalue.  Inside a function it has automatic
   storage and the enclosing block's lifetime (it is one of the block's
   variables, so its pointer slots are wiped at block exit like any local).
   The object is a TARGET_EXPR: evaluated once even when the expression is
   used twice (an array decaying to {addr, base, size}).  At file scope it
   is a static object.  */
/* (static compound literal object, its initializer before lowering): a
   static initializer that uses the literal as an element takes the
   initializer itself (GCC), lowered for the object being initialized.  */
static GTY(()) vec<tree, va_gc> *cl_raw_inits;

static ex
compound_literal (tree type, location_t loc)
{
  /* Inside a static object's initializer the literal has static storage
     too (the kernel's __RAW_SPIN_LOCK_UNLOCKED in DEFINE_MUTEX).  */
  bool file_scope = cur_fn == NULL || static_init_depth > 0;
  if (TREE_CODE (type) == FUNCTION_TYPE || VOID_TYPE_P (type))
    {
      error_at (loc, "compound literal has invalid type");
      skip_to_semi ();
      return err ();
    }
  tree init = parse_initializer (type, file_scope);
  if (error_p (init))
    return err ();
  type = complete_array_from_init (type, init);
  if (!COMPLETE_TYPE_P (type)
      || TREE_CODE (TYPE_SIZE_UNIT (type)) != INTEGER_CST)
    {
      error_at (loc, "compound literal has variable or incomplete size");
      return err ();
    }
  static int counter;
  char name[32];
  snprintf (name, sizeof name, "__tzd_cl.%d", counter++);
  tree var = build_decl (loc, VAR_DECL,
			 file_scope ? get_identifier (name) : NULL_TREE, type);
  DECL_ARTIFICIAL (var) = 1;
  TREE_USED (var) = 1;
  TREE_ADDRESSABLE (var) = 1;
  if (file_scope)
    {
      TREE_STATIC (var) = 1;
      TREE_PUBLIC (var) = 0;
      vec_safe_push (cl_raw_inits, var);
      vec_safe_push (cl_raw_inits, unshare_expr (init));
      if (has_slots_p (type) || has_fnholds_p (type))
	init = lower_slot_init (init, type, var, true, NULL, loc);
      if (error_p (init))
	return err ();
      DECL_INITIAL (var) = init;
      rest_of_decl_compilation (var, 1, 0);
      return mk (var, type, true);
    }
  DECL_CONTEXT (var) = cur_fn->decl;
  /* The gimplifier adds a TARGET_EXPR's object to the function's locals
     (it must not be in a BIND_EXPR); the enclosing block still wipes it
     at exit (any type: TK 2026-11-07).  */
  cl_wipe_add (var);
  tree stmts = alloc_stmt_list ();
  tree slot_stmts = alloc_stmt_list ();
  if (has_slots_p (type))
    init = lower_slot_init (init, type, var, false, &slot_stmts, loc);
  if (error_p (init))
    return err ();
  if (init && !(TZD_MSLOT_P (type)))
    append_to_statement_list_force (build2_loc (loc, INIT_EXPR, type, var,
						init), &stmts);
  append_to_statement_list_force (slot_stmts, &stmts);
  tree te = build4 (TARGET_EXPR, type, var, stmts, NULL_TREE, NULL_TREE);
  TREE_SIDE_EFFECTS (te) = 1;
  SET_EXPR_LOCATION (te, loc);
  return mk (te, type, true);
}

static ex
parse_cast (void)
{
  if (is_p ("(") && typename_start_p (1))
    {
      location_t loc = loc0 ();
      next ();
      tree t = parse_type_name ();
      expect (")");
      if (is_p ("{"))
	return parse_postfix_ops (compound_literal (t, loc));
      ex e = parse_cast ();
      bool ice = e.ice;
      ex r = convert_to (e, t, loc, true);
      r.ice = ice && !bad (r)
	      && (INTEGRAL_TYPE_P (r.type) || TZD_MPTR_P (r.type));
      return r;
    }
  return parse_unary ();
}

static int
binop_prec (void)
{
  const tzd_token &t = peek ();
  if (t.kind != TZD_PUNCT)
    return -1;
  static const struct { const char *op; int prec; } tab[] = {
    { "||", 1 }, { "&&", 2 }, { "|", 3 }, { "^", 4 }, { "&", 5 },
    { "==", 6 }, { "!=", 6 }, { "<", 7 }, { ">", 7 }, { "<=", 7 },
    { ">=", 7 }, { "<<", 8 }, { ">>", 8 }, { "+", 9 }, { "-", 9 },
    { "*", 10 }, { "/", 10 }, { "%", 10 }
  };
  for (unsigned i = 0; i < ARRAY_SIZE (tab); i++)
    if (strcmp (t.text, tab[i].op) == 0)
      return tab[i].prec;
  return -1;
}

static ex
parse_binary (int min_prec)
{
  ex lhs = parse_cast ();
  for (;;)
    {
      int prec = binop_prec ();
      if (prec < min_prec)
	return lhs;
      const char *op = peek ().text;
      location_t loc = loc0 ();
      next ();
      ex rhs = parse_binary (prec + 1);
      lhs = binop (op, lhs, rhs, loc);
    }
}

/* Type of a ?: result.  */
static ex
parse_cond (void)
{
  ex c = parse_binary (1);
  if (!is_p ("?"))
    return c;
  location_t loc = loc0 ();
  next ();
  ex a;
  if (is_p (":"))
    {
      /* GNU a ?: b -- a evaluated once, its value if true.  */
      c = rvalue (c, loc);
      if (!bad (c))
	{
	  if (TZD_MPTR_P (c.type))
	    {
	      parts (c);
	      c.pa = save_expr (c.pa);
	      c.pb = save_expr (c.pb);
	      c.ps = save_expr (c.ps);
	    }
	  else
	    c.t = save_expr (c.t);
	}
      a = c;
    }
  else
    a = rvalue (parse_expr (), loc);
  expect (":");
  ex b = rvalue (parse_cond (), loc);
  tree cond = truth (c, loc);
  if (bad (a) || bad (b) || error_p (cond))
    return err ();
  bool ice = c.ice && a.ice && b.ice;
  /* A null pointer constant: an integer constant expression with value 0,
     possibly cast to void * (C11 6.3.2.3).  */
  auto npc = [] (const ex &e) {
    if (!e.ice)
      return false;
    if (INTEGRAL_TYPE_P (e.type))
      return null_const_p (e);
    return TZD_MPTR_P (e.type) && VOID_TYPE_P (TZD_MPTR_TARGET (e.type))
	   && null_mptr_p (e);
  };
  tree type;
  if (arith_p (a.type) && arith_p (b.type))
    type = common_arith (a.type, b.type);
  else if (TZD_MPTR_P (a.type) && TZD_MPTR_P (b.type))
    {
      /* One side a null pointer constant: the other's type; else a void *
	 side makes the result void * (C11 6.5.15).  */
      if (npc (a))
	type = b.type;
      else if (npc (b))
	type = a.type;
      else if (VOID_TYPE_P (TZD_MPTR_TARGET (b.type)))
	type = b.type;
      else
	type = a.type;
    }
  else if (TZD_MPTR_P (a.type) && null_const_p (b))
    type = a.type;
  else if (TZD_MPTR_P (b.type) && null_const_p (a))
    type = b.type;
  else if (fnptr_p (a.type) && fnptr_p (b.type)
	   && fn_abi_mismatch_p (TREE_TYPE (a.type), TREE_TYPE (b.type)))
    {
      /* A host function (raw pointers) and a TzD function (managed):
	 the result is the TzD one's type, so the host function gets its
	 thunk and a call through the result passes managed pointers
	 (gnulib's `fnmatch : fnmatch_no_wildcards`, task #71).  */
      auto managed_abi = [] (tree fnptr)
	{
	  for (tree at = TYPE_ARG_TYPES (TREE_TYPE (fnptr)); at;
	       at = TREE_CHAIN (at))
	    if (TZD_MPTR_P (TREE_VALUE (at)))
	      return true;
	  return TZD_MPTR_P (TREE_TYPE (TREE_TYPE (fnptr)));
	};
      type = managed_abi (b.type) && !managed_abi (a.type) ? b.type : a.type;
    }
  else if (fnptr_p (a.type)
	   && (fnptr_p (b.type) || null_const_p (b) || null_mptr_p (b)))
    type = a.type;
  else if (fnptr_p (b.type) && (null_const_p (a) || null_mptr_p (a)))
    type = b.type;
  else if (VOID_TYPE_P (a.type) || VOID_TYPE_P (b.type))
    /* GNU C: one side void makes the result void (pgtable.h's
       c ? native_pgd_clear (pgd) : 0).  */
    type = void_type_node;
  else if (TYPE_MAIN_VARIANT (a.type) == TYPE_MAIN_VARIANT (b.type))
    type = a.type;
  else
    {
      error_at (loc, "type mismatch in conditional expression");
      return err ();
    }
  type = TYPE_MAIN_VARIANT (type);
  ex ca = convert_to (a, type, loc), cb = convert_to (b, type, loc);
  if (bad (ca) || bad (cb))
    return err ();
  ex r = mk (fold_build3_loc (loc, COND_EXPR, vtype (type), cond, value (ca),
			      value (cb)), type);
  r.ice = ice;
  return r;
}

/* Is T a reference to an object in memory (so its address can be taken
   for the shadow copy)?  */
static bool
memory_ref_p (tree t)
{
  while (TREE_CODE (t) == NOP_EXPR || TREE_CODE (t) == VIEW_CONVERT_EXPR)
    t = TREE_OPERAND (t, 0);
  if (TREE_CODE (t) == COMPOUND_EXPR)
    return memory_ref_p (TREE_OPERAND (t, 1));
  return (DECL_P (t) || TREE_CODE (t) == COMPONENT_REF
	  || TREE_CODE (t) == ARRAY_REF || TREE_CODE (t) == INDIRECT_REF
	  || TREE_CODE (t) == MEM_REF || TREE_CODE (t) == TARGET_EXPR);
}

/* L = R for a struct/union/array containing managed pointer slots: copy the
   bytes, then the shadow entries.  Returns the assignment as an rvalue
   tree of the aggregate type.  */
static tree
aggregate_copy (tree l, tree r, location_t loc)
{
  tree type = TREE_TYPE (l);
  if (!memory_ref_p (r))
    {
      /* A struct value with pointers that is not in memory (call result,
	 ?:): its shadow entries are gone.  Provisional: not supported
	 (task #1 (b)).  */
      sorry_at (loc, "tzd: copying a struct that contains pointers from a "
		"value that is not an object (task #1)");
      return error_mark_node;
    }
  /* (side effects, object): a lowered call's result object (sv).  */
  tree pre = NULL_TREE;
  while (TREE_CODE (r) == COMPOUND_EXPR)
    {
      pre = pre ? build2 (COMPOUND_EXPR, void_type_node, pre,
			  TREE_OPERAND (r, 0))
		: TREE_OPERAND (r, 0);
      r = TREE_OPERAND (r, 1);
    }
  if (TREE_CODE (r) == TARGET_EXPR)
    {
      /* A compound literal: initialize it first, then copy from it.  */
      tree slot = TARGET_EXPR_SLOT (r);
      tree init = fold_convert (void_type_node, r);
      pre = pre ? build2 (COMPOUND_EXPR, void_type_node, pre, init) : init;
      r = slot;
    }
  l = stabilize_reference (l);
  r = stabilize_reference (r);
  mark_addressable_tree (l);
  mark_addressable_tree (r);
  tree m = build2_loc (loc, MODIFY_EXPR, type, l, r);
  if (pre)
    m = build2 (COMPOUND_EXPR, type, pre, m);
  TREE_SIDE_EFFECTS (m) = 1;
  tree c = shadow_copy (build_fold_addr_expr_loc (loc, l),
			build_fold_addr_expr_loc (loc, r),
			TYPE_SIZE_UNIT (type), loc);
  tree both = build2 (COMPOUND_EXPR, void_type_node, m, c);
  return build2 (COMPOUND_EXPR, type, both, l);
}

static ex assign_store (ex lhs, ex rhs, int k, char *opbuf, location_t loc);

static ex
parse_assign (void)
{
  ex lhs = parse_cond ();
  static const char *const ops[] = {
    "=", "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=", 0
  };
  const tzd_token &t = peek ();
  if (t.kind != TZD_PUNCT)
    return lhs;
  int k;
  for (k = 0; ops[k]; k++)
    if (strcmp (t.text, ops[k]) == 0)
      break;
  if (!ops[k])
    return lhs;
  location_t loc = t.loc;
  char opbuf[4];
  strcpy (opbuf, ops[k]);
  next ();
  ex rhs = parse_assign ();
  if (bad (lhs) || bad (rhs))
    return err ();
  if (!lhs.lv || TREE_CODE (lhs.type) == ARRAY_TYPE
      || TREE_CODE (lhs.type) == FUNCTION_TYPE)
    {
      error_at (loc, "lvalue required as left operand of assignment");
      return err ();
    }
  if (TYPE_READONLY (lhs.type))
    error_at (loc, "assignment of read-only location");

  /* Write groups (task #79): the test comes before the store.  */
  tree guard = grp_store_guard (&lhs.t, loc);
  return grp_prepend (guard, assign_store (lhs, rhs, k, opbuf, loc));
}

/* The store of parse_assign: LHS OP RHS, OP being ops[K] (in OPBUF).  */
static ex
assign_store (ex lhs, ex rhs, int k, char *opbuf, location_t loc)
{
  tree l = lhs.t;
  ex val;
  if (raw_data_ptr_p (lhs.type) && !native_region_p ()
      && !(TREE_CODE (l) == COMPONENT_REF
	   && DECL_BIT_FIELD (TREE_OPERAND (l, 1)))
      /* A machine register holds only the address (below).  */
      && !(VAR_P (l) && DECL_HARD_REGISTER (l)))
    {
      /* User code stores a pointer into host memory (a field of a host
	 struct, a host variable): write the address and record its bounds
	 there, so a later load can use them while the 8 bytes still match
	 (task #1).  The expression's value keeps the bounds.  */
      l = stabilize_reference (l);
      ex mv;
      if (k == 0)
	mv = rvalue (rhs, loc);
      else
	{
	  opbuf[strlen (opbuf) - 1] = 0;
	  mv = binop (opbuf, rvalue (mk (l, lhs.type, true), loc), rhs, loc);
	}
      if (!bad (mv) && !TZD_MPTR_P (mv.type))
	mv = convert_to (mv, tzd_mptr_type (TREE_TYPE (lhs.type)), loc);
      if (bad (mv))
	return err ();
      init_shadow_decls ();
      tree v = save_expr (value (mv));
      ex vm = mk (v, mv.type);
      ex raw = convert_to (vm, lhs.type, loc);
      if (bad (raw))
	return err ();
      mark_addressable_tree (l);
      tree st = build2_loc (loc, MODIFY_EXPR, TREE_TYPE (l), l, value (raw));
      TREE_SIDE_EFFECTS (st) = 1;
      tree rec = build_call_expr_loc (loc, tzd_sh_record_decl, 2,
				      fold_convert (const_ptr_type_node,
						    build_fold_addr_expr_loc
						      (loc, l)),
				      build1 (VIEW_CONVERT_EXPR,
					      tzd_mrec_type (void_type_node),
					      v));
      tree m = build2 (COMPOUND_EXPR, TREE_TYPE (v),
		       build2 (COMPOUND_EXPR, void_type_node, st, rec), v);
      TREE_SIDE_EFFECTS (m) = 1;
      return mk (m, mv.type);
    }
  if (k == 0 && VAR_P (l) && DECL_HARD_REGISTER (l)
      && raw_data_ptr_p (lhs.type))
    {
      /* Pointer into a machine register: the register gets pointer();
	 the expression's value is the managed right-hand side.  */
      ex mv = rvalue (rhs, loc);
      if (!bad (mv) && !TZD_MPTR_P (mv.type))
	mv = convert_to (mv, tzd_mptr_type (TREE_TYPE (lhs.type)), loc,
			 true);
      if (bad (mv))
	return err ();
      tree v = save_expr (value (mv));
      ex raw = convert_to (mk (v, mv.type), lhs.type, loc);
      if (bad (raw))
	return err ();
      tree st = build2_loc (loc, MODIFY_EXPR, TREE_TYPE (l), l, value (raw));
      TREE_SIDE_EFFECTS (st) = 1;
      tree m = build2 (COMPOUND_EXPR, TREE_TYPE (v), st, v);
      TREE_SIDE_EFFECTS (m) = 1;
      return mk (m, mv.type);
    }
  if (const char *const *hl = (k == 0 && !flag_tzd_kernel)
				? fnptr_holds_list (l) : NULL)
    {
      /* A `#pragma tzd fnptr_holds` member (task #70): a data pointer cast
	 to the function type is recorded with its bounds and type id; a
	 copy from another such member copies the record; a function (or
	 null) drops any record.  */
      init_shadow_decls ();
      l = stabilize_reference (l);
      mark_addressable_tree (l);
      tree slot = build_fold_addr_expr_loc (loc, l);
      tree src = rhs.t;
      if (src)
	STRIP_NOPS (src);
      const char *const *rl = rhs.fdata ? NULL : fnptr_holds_list (src);
      if (rl)
	{
	  src = stabilize_reference (src);
	  rhs = mk (src, rhs.type, rhs.lv);
	}
      if ((rhs.fdata || rhs.fdtype) && !fnptr_holds_allows (hl, rhs.fdtype))
	{
	  error_at (loc, "%<#pragma tzd fnptr_holds%> for this member does not "
		    "list the type %qT", rhs.fdtype);
	  return err ();
	}
      if (rhs.fdtype && !rhs.fdata)
	{
	  error_at (loc, "data pointer stored into a %<#pragma tzd "
		    "fnptr_holds%> member outside a function");
	  return err ();
	}
      ex v0 = convert_to (rhs, lhs.type, loc);
      if (bad (v0))
	return err ();
      tree v = save_expr (value (v0));
      tree st = build2_loc (loc, MODIFY_EXPR, TREE_TYPE (l), l, v);
      TREE_SIDE_EFFECTS (st) = 1;
      tree after;
      if (rhs.fdata)
	after = build_call_expr_loc
	  (loc, tzd_sh_record_typed_decl, 3, fold_convert (ptr_type_node, slot),
	   build1 (VIEW_CONVERT_EXPR, tzd_mrec_type (void_type_node), rhs.fdata),
	   build_int_cstu (size_type_node, fnptr_holds_id (rhs.fdtype)));
      else if (rl)
	after = build_call_expr_loc
	  (loc, tzd_sh_copy_decl, 3, fold_convert (ptr_type_node, slot),
	   fold_convert (const_ptr_type_node, build_fold_addr_expr_loc (loc, src)),
	   size_int (8));
      else
	/* A null record drops the entry (libc2d tzd_sh_record_typed).  */
	after = build_call_expr_loc
	  (loc, tzd_sh_record_typed_decl, 3, fold_convert (ptr_type_node, slot),
	   build_zero_cst (tzd_mrec_type (void_type_node)),
	   build_int_cstu (size_type_node, 0));
      tree m = build2 (COMPOUND_EXPR, TREE_TYPE (v),
		       build2 (COMPOUND_EXPR, void_type_node, st, after), v);
      TREE_SIDE_EFFECTS (m) = 1;
      return mk (m, TYPE_MAIN_VARIANT (lhs.type));
    }
  if (k == 0)
    val = convert_to (rhs, lhs.type, loc);
  else
    {
      l = stabilize_reference (l);
      opbuf[strlen (opbuf) - 1] = 0;
      ex cur = mk (l, lhs.type, true);
      val = convert_to (binop (opbuf, cur, rhs, loc), lhs.type, loc);
    }
  if (bad (val))
    return err ();
  tree type = TYPE_MAIN_VARIANT (lhs.type);
  if (TZD_MSLOT_P (TREE_TYPE (l)))
    {
      /* Managed pointer slot in memory: store the 8 bytes and the shadow
	 entry together (task #1 (b)).  */
      tree v = save_expr (value (val));
      tree m = build2 (COMPOUND_EXPR, TREE_TYPE (v), slot_store (l, v, loc), v);
      TREE_SIDE_EFFECTS (m) = 1;
      return mk (m, type);
    }
  if (RECORD_OR_UNION_TYPE_P (type) && has_slots_p (type))
    return mk (aggregate_copy (l, value (val), loc), type);
  /* Heap bytes stored to are written (task #8).  Marked before the store:
     should the store's own check trap, the program stops anyway.  */
  tree mark = hp_store_mark (l, type, loc);
  tree m = build2_loc (loc, MODIFY_EXPR, vtype (type), l, value (val));
  TREE_SIDE_EFFECTS (m) = 1;
  if (mark)
    {
      m = build2 (COMPOUND_EXPR, TREE_TYPE (m), mark, m);
      TREE_SIDE_EFFECTS (m) = 1;
    }
  return mk (m, type);
}

static ex
parse_expr (void)
{
  ex e = parse_assign ();
  while (is_p (","))
    {
      location_t loc = loc0 ();
      next ();
      ex r = rvalue (parse_assign (), loc);
      if (bad (e) || bad (r))
	{
	  e = err ();
	  continue;
	}
      tree v = value (r);
      e = mk (build2_loc (loc, COMPOUND_EXPR, TREE_TYPE (v), value_void (e),
			  v), r.type);
    }
  return e;
}

/* ------------------------------------------------------------------ */
/* Initializers.  */

static tree parse_initializer (tree type, bool is_static);

static tree
string_init (tree type, ex s, location_t loc)
{
  tree str = s.t;
  tree dom = TYPE_DOMAIN (type);
  /* In elements: wide strings (L"") have 2- or 4-byte ones.  */
  HOST_WIDE_INT width = int_size_in_bytes (TREE_TYPE (TREE_TYPE (str)));
  HOST_WIDE_INT have = TREE_STRING_LENGTH (str) / width;
  if (dom && TYPE_MAX_VALUE (dom))
    {
      HOST_WIDE_INT want = tree_to_shwi (TYPE_MAX_VALUE (dom)) + 1;
      if (have - 1 > want)
	warning_at (loc, 0, "initializer-string for array is too long");
      tree ns = build_string (MIN (have, want) * width,
			      TREE_STRING_POINTER (str));
      TREE_TYPE (ns) = type;
      TREE_CONSTANT (ns) = 1;
      TREE_STATIC (ns) = 1;
      return ns;
    }
  TREE_TYPE (str) = type;
  return str;
}

/* Brace initializers, with C99 designators (`[N] =`, `.f =`, chains such as
   `.a.b[2] =`).  Elements are kept in layout order, a later initializer of
   the same element replaces the earlier one, and an index outside a
   complete array is an error: excess elements are not silently dropped
   (and never written past the object).  */

/* Path of FIELD_DECLs from ST to its member ID, through anonymous members.  */
static bool
field_path (tree st, tree id, vec<tree> *path)
{
  for (tree f = TYPE_FIELDS (st); f; f = DECL_CHAIN (f))
    {
      if (TREE_CODE (f) != FIELD_DECL)
	continue;
      if (DECL_NAME (f) == id)
	{
	  path->safe_push (f);
	  return true;
	}
      if (DECL_NAME (f) == NULL_TREE && RECORD_OR_UNION_TYPE_P (TREE_TYPE (f)))
	{
	  path->safe_push (f);
	  if (field_path (TREE_TYPE (f), id, path))
	    return true;
	  path->pop ();
	}
    }
  return false;
}

/* Layout order key of element IDX of aggregate TYPE.  */
static unsigned HOST_WIDE_INT
init_key (tree type, tree idx)
{
  if (TREE_CODE (type) == ARRAY_TYPE)
    return tree_to_uhwi (idx);
  unsigned HOST_WIDE_INT n = 0;
  for (tree f = TYPE_FIELDS (type); f && f != idx; f = DECL_CHAIN (f))
    n++;
  return n;
}

static tree
init_get (vec<constructor_elt, va_gc> *v, tree type, tree idx)
{
  unsigned i;
  constructor_elt *ce;
  FOR_EACH_VEC_SAFE_ELT (v, i, ce)
    if (init_key (type, ce->index) == init_key (type, idx))
      return ce->value;
  return NULL_TREE;
}

static void
init_put (vec<constructor_elt, va_gc> *&v, tree type, tree idx, tree val)
{
  if (TREE_CODE (type) == UNION_TYPE)
    vec_safe_truncate (v, 0);         /* one member of a union at a time */
  unsigned HOST_WIDE_INT k = init_key (type, idx);
  unsigned i;
  constructor_elt *ce;
  FOR_EACH_VEC_SAFE_ELT (v, i, ce)
    {
      unsigned HOST_WIDE_INT ck = init_key (type, ce->index);
      if (ck == k)
	{
	  ce->value = val;
	  return;
	}
      if (ck > k)
	break;
    }
  constructor_elt e = { idx, val };
  vec_safe_insert (v, i, e);
}

/* Is IDX a valid index of array TYPE?  An incomplete array accepts any.  */
static bool
init_index_ok (tree type, unsigned HOST_WIDE_INT idx)
{
  tree dom = TYPE_DOMAIN (type);
  if (!dom || !TYPE_MAX_VALUE (dom))
    return true;
  if (TREE_CODE (TYPE_MAX_VALUE (dom)) != INTEGER_CST)
    return false;
  return idx <= tree_to_uhwi (TYPE_MAX_VALUE (dom));
}

/* Parse the designators before `=`.  IDX gets each element (an index or a
   FIELD_DECL), TY the aggregate type it belongs to.  */
static bool
parse_designation (tree type, vec<tree> *idx, vec<tree> *ty,
		   unsigned *rk, tree *rhi)
{
  tree cur = type;
  *rhi = NULL_TREE;
  for (;;)
    {
      location_t l = loc0 ();
      if (accept ("["))
	{
	  if (TREE_CODE (cur) != ARRAY_TYPE)
	    {
	      error_at (l, "array index in non-array initializer");
	      return false;
	    }
	  ex e = parse_assign ();
	  if (bad (e))
	    return false;
	  e = rvalue (e, l);
	  tree t = bad (e) ? NULL_TREE : fold (value (e));
	  if (!t || TREE_CODE (t) != INTEGER_CST || !INTEGRAL_TYPE_P (TREE_TYPE (t)))
	    {
	      error_at (l, "array index in initializer is not an integer "
			"constant");
	      return false;
	    }
	  tree hi = NULL_TREE;
	  if (accept ("..."))
	    {
	      /* GNU [lo ... hi] = v: every element from lo to hi.  */
	      ex h = parse_assign ();
	      if (bad (h))
		return false;
	      h = rvalue (h, l);
	      hi = bad (h) ? NULL_TREE : fold (value (h));
	      if (!hi || TREE_CODE (hi) != INTEGER_CST
		  || !INTEGRAL_TYPE_P (TREE_TYPE (hi)))
		{
		  error_at (l, "array index in initializer is not an integer "
			    "constant");
		  return false;
		}
	      if (*rhi)
		{
		  sorry_at (l, "more than one range in one designator");
		  return false;
		}
	      if (tree_int_cst_lt (hi, t))
		{
		  error_at (l, "empty index range in initializer");
		  return false;
		}
	    }
	  if (tree_int_cst_sgn (t) < 0 || !tree_fits_uhwi_p (t)
	      || !init_index_ok (cur, tree_to_uhwi (t))
	      || (hi && (!tree_fits_uhwi_p (hi)
			 || !init_index_ok (cur, tree_to_uhwi (hi)))))
	    {
	      error_at (l, "array index in initializer exceeds array bounds");
	      return false;
	    }
	  if (hi)
	    {
	      *rk = idx->length ();
	      *rhi = hi;
	    }
	  if (!expect ("]"))
	    return false;
	  ty->safe_push (cur);
	  idx->safe_push (size_int (tree_to_uhwi (t)));
	  cur = TREE_TYPE (cur);
	}
      else if (accept ("."))
	{
	  if (!RECORD_OR_UNION_TYPE_P (cur) || TZD_MPTR_P (cur))
	    {
	      error_at (l, "field name not in record or union initializer");
	      return false;
	    }
	  if (peek ().kind != TZD_IDENT)
	    {
	      error_at (loc0 (), "expected identifier");
	      return false;
	    }
	  tree id = get_identifier (peek ().text);
	  next ();
	  auto_vec<tree> path;
	  if (!COMPLETE_TYPE_P (cur) || !field_path (cur, id, &path))
	    {
	      error_at (l, "unknown field %qE specified in initializer", id);
	      return false;
	    }
	  for (tree f : path)
	    {
	      ty->safe_push (cur);
	      idx->safe_push (f);
	      cur = TREE_TYPE (f);
	    }
	}
      else
	break;
    }
  return expect ("=");
}

/* Store VAL at the element named by IDX[K..] inside V (of TYPE), building
   or extending the intermediate initializers.  */
static bool
init_put_path (vec<constructor_elt, va_gc> *&v, tree type, vec<tree> &idx,
	       vec<tree> &ty, unsigned k, tree val, location_t loc)
{
  tree here = idx[k];
  if (k + 1 == idx.length ())
    {
      init_put (v, type, here, val);
      return true;
    }
  tree sub = TREE_CODE (here) == FIELD_DECL ? TREE_TYPE (here)
					   : TREE_TYPE (type);
  tree old = init_get (v, type, here);
  if (old && (TREE_CODE (old) != CONSTRUCTOR
	      || TYPE_MAIN_VARIANT (TREE_TYPE (old)) != TYPE_MAIN_VARIANT (sub)))
    {
      sorry_at (loc, "designator into a member that an expression "
		"initialized");
      return false;
    }
  vec<constructor_elt, va_gc> *sv = old ? CONSTRUCTOR_ELTS (old) : NULL;
  if (!init_put_path (sv, sub, idx, ty, k + 1, val, loc))
    return false;
  tree c = old ? old : build_constructor (sub, NULL);
  CONSTRUCTOR_ELTS (c) = sv;
  recompute_constructor_flags (c);
  init_put (v, type, here, c);
  return true;
}

static tree parse_initializer (tree type, bool is_static);
static tree init_from_expr (ex e, tree type, bool is_static, location_t loc);
static tree init_finish (tree val, bool is_static, location_t loc);

/* An aggregate a positional initializer may fill without braces.  */
static bool
elidable_type_p (tree t)
{
  return TREE_CODE (t) == ARRAY_TYPE
	 || (RECORD_OR_UNION_TYPE_P (t) && !TZD_MPTR_P (t));
}

/* Brace elision (C 6.7.9p20): the members of aggregate TYPE taken in
   order from the enclosing list, without braces.  *PEND, when HAVE, is
   an already parsed expression for the first scalar.  Stops at "}", at a
   designator, or when TYPE is full; the comma after its last member is
   left for the enclosing list.  */
static tree
elide_init (tree type, bool is_static, ex *pend, bool *have)
{
  location_t loc = loc0 ();
  vec<constructor_elt, va_gc> *v = NULL;
  bool array = TREE_CODE (type) == ARRAY_TYPE;
  if (!array && !COMPLETE_TYPE_P (type))
    {
      error_at (loc, "variable has incomplete type");
      return error_mark_node;
    }
  unsigned HOST_WIDE_INT i = 0;
  tree f = array ? NULL_TREE : TYPE_FIELDS (type);
  while (!array && f && TREE_CODE (f) != FIELD_DECL)
    f = DECL_CHAIN (f);
  for (;;)
    {
      if (array ? !TYPE_DOMAIN (type) || !init_index_ok (type, i)
		: f == NULL_TREE)
	break;
      if (!*have && (is_p ("}") || is_p ("[") || is_p (".")))
	break;
      tree et = array ? TREE_TYPE (type) : TREE_TYPE (f);
      tree val;
      if (*have)
	{
	  if (elidable_type_p (et)
	      && TYPE_MAIN_VARIANT (et) != TYPE_MAIN_VARIANT (pend->type))
	    val = elide_init (et, is_static, pend, have);
	  else
	    {
	      *have = false;
	      val = init_from_expr (*pend, et, is_static, loc);
	    }
	}
      else if (elidable_type_p (et) && !is_p ("{")
	       && !(TREE_CODE (et) == ARRAY_TYPE
		    && peek ().kind == TZD_STRING))
	{
	  ex e = parse_assign ();
	  if (bad (e))
	    return error_mark_node;
	  if (TYPE_MAIN_VARIANT (et) == TYPE_MAIN_VARIANT (e.type))
	    val = init_from_expr (e, et, is_static, loc);
	  else
	    {
	      bool h = true;
	      val = elide_init (et, is_static, &e, &h);
	    }
	}
      else
	val = parse_initializer (et, is_static);
      if (error_p (val))
	return error_mark_node;
      if (array)
	init_put (v, type, size_int (i++), val);
      else
	{
	  init_put (v, type, f, val);
	  f = TREE_CODE (type) == UNION_TYPE ? NULL_TREE : DECL_CHAIN (f);
	  while (f && TREE_CODE (f) != FIELD_DECL)
	    f = DECL_CHAIN (f);
	}
      /* More members: the next element of the list is ours if it is not
	 the end or a designator.  */
      bool more = array ? (TYPE_DOMAIN (type) && init_index_ok (type, i))
			: f != NULL_TREE;
      if (!more || !is_p (",") || is_p ("}", 1) || is_p ("[", 1)
	  || is_p (".", 1))
	break;
      next ();
    }
  return build_constructor (type, v);
}

static tree
parse_brace_init (tree type, bool is_static)
{
  location_t loc = loc0 ();
  next ();   /* { */
  vec<constructor_elt, va_gc> *v = NULL;
  bool array = TREE_CODE (type) == ARRAY_TYPE;
  if (array && peek ().kind == TZD_STRING
      && INTEGRAL_TYPE_P (TREE_TYPE (type))
      && TYPE_PRECISION (TREE_TYPE (type)) == CHAR_TYPE_SIZE)
    {
      /* char a[] = { "abc" }: the string literal may be enclosed in
	 braces, alone.  */
      tree val = parse_initializer (type, is_static);
      accept (",");
      expect ("}");
      return val;
    }
  if (array || (RECORD_OR_UNION_TYPE_P (type) && !TZD_MPTR_P (type)))
    {
      if (!array && !COMPLETE_TYPE_P (type))
	{
	  error_at (loc, "variable has incomplete type");
	  return error_mark_node;
	}
      /* Next positional element: an index, or a field.  */
      unsigned HOST_WIDE_INT i = 0;
      tree f = array ? NULL_TREE : TYPE_FIELDS (type);
      while (!array && f && TREE_CODE (f) != FIELD_DECL)
	f = DECL_CHAIN (f);
      bool pos_ok = true;     /* false after a nested designator */
      while (!is_p ("}") && peek ().kind != TZD_EOF)
	{
	  location_t eloc = loc0 ();
	  if (is_p ("[") || is_p ("."))
	    {
	      auto_vec<tree> idx, ty;
	      unsigned rk = 0;
	      tree rhi;
	      if (!parse_designation (type, &idx, &ty, &rk, &rhi))
		{
		  skip_to_semi ();
		  return error_mark_node;
		}
	      tree last = idx.last ();
	      tree et = TREE_CODE (last) == FIELD_DECL ? TREE_TYPE (last)
						       : TREE_TYPE (ty.last ());
	      tree val = parse_initializer (et, is_static);
	      if (error_p (val))
		return error_mark_node;
	      if (rhi)
		{
		  /* [lo ... hi]: the same value at each index (a fresh copy
		     each time).  */
		  unsigned HOST_WIDE_INT lo = tree_to_uhwi (idx[rk]);
		  unsigned HOST_WIDE_INT hi = tree_to_uhwi (rhi);
		  for (unsigned HOST_WIDE_INT j = lo; j <= hi; j++)
		    {
		      idx[rk] = size_int (j);
		      if (!init_put_path (v, type, idx, ty, 0,
					  j == lo ? val : unshare_expr (val),
					  eloc))
			return error_mark_node;
		    }
		}
	      else if (!init_put_path (v, type, idx, ty, 0, val, eloc))
		return error_mark_node;
	      /* C continues after the designated element.  After a nested
		 designator that is inside the inner aggregate (brace
		 elision): not supported, so a positional element there is
		 an error rather than a guess.  */
	      pos_ok = idx.length () == 1;
	      if (array)
		i = tree_to_uhwi (idx[0]) + 1;
	      else
		{
		  f = TREE_CODE (type) == UNION_TYPE ? NULL_TREE
						     : DECL_CHAIN (idx[0]);
		  while (f && TREE_CODE (f) != FIELD_DECL)
		    f = DECL_CHAIN (f);
		}
	    }
	  else
	    {
	      if (!pos_ok)
		{
		  error_at (eloc, "positional initializer after a nested "
			    "designator; designate this element too");
		  skip_to_semi ();
		  return error_mark_node;
		}
	      if (array ? !init_index_ok (type, i) : f == NULL_TREE)
		{
		  error_at (eloc, array ? "excess elements in array initializer"
				  : "excess elements in struct initializer");
		  skip_to_semi ();
		  return error_mark_node;
		}
	      tree et = array ? TREE_TYPE (type) : TREE_TYPE (f);
	      tree val;
	      if (elidable_type_p (et) && !is_p ("{")
		  && !(TREE_CODE (et) == ARRAY_TYPE
		       && peek ().kind == TZD_STRING))
		{
		  /* No braces: the expression itself if it has the member's
		     type, else brace elision ({0} for a struct whose first
		     member is an aggregate).  */
		  ex e = parse_assign ();
		  if (bad (e))
		    return error_mark_node;
		  if (TYPE_MAIN_VARIANT (et) == TYPE_MAIN_VARIANT (e.type))
		    val = init_from_expr (e, et, is_static, eloc);
		  else
		    {
		      bool h = true;
		      val = elide_init (et, is_static, &e, &h);
		    }
		}
	      else
		val = parse_initializer (et, is_static);
	      if (error_p (val))
		return error_mark_node;
	      if (array)
		init_put (v, type, size_int (i++), val);
	      else
		{
		  init_put (v, type, f, val);
		  f = TREE_CODE (type) == UNION_TYPE ? NULL_TREE
						     : DECL_CHAIN (f);
		  while (f && TREE_CODE (f) != FIELD_DECL)
		    f = DECL_CHAIN (f);
		}
	    }
	  if (!accept (","))
	    break;
	}
      expect ("}");
      return build_constructor (type, v);
    }
  /* Scalar in braces.  */
  tree val = parse_initializer (type, is_static);
  accept (",");
  expect ("}");
  return val;
}

static tree parse_initializer_1 (tree type, bool is_static);

static tree
parse_initializer (tree type, bool is_static)
{
  if (is_static)
    static_init_depth++;
  tree r = parse_initializer_1 (type, is_static);
  if (is_static)
    static_init_depth--;
  return r;
}

/* A static managed pointer whose size is __tzd_size.NAME (&x[i] for an
   extern array or object defined in another unit): the size is known only
   at link time.  The tzd_shinit entry then holds the symbol's address
   plus 2^63 (a relocation addend); libc2d reads the size from the symbol
   before main (tzd_sh_init_static).  No object is 2^63 bytes or larger.  */
static tree
static_size_sym_init (tree val)
{
  if (TREE_CODE (val) != CONSTRUCTOR || !TZD_MPTR_P (TREE_TYPE (val))
      || CONSTRUCTOR_NELTS (val) != 3)
    return val;
  tree sz = CONSTRUCTOR_ELT (val, 2)->value;
  tree d = sz;
  STRIP_NOPS (d);
  if (!VAR_P (d))
    return val;
  bool sym = false;
  for (unsigned i = 0; i + 1 < vec_safe_length (size_syms); i += 2)
    if ((*size_syms)[i + 1] == d)
      sym = true;
  if (!sym)
    return val;
  tree a = build_fold_addr_expr (d);
  tree enc = fold_build2 (PLUS_EXPR, size_type_node,
			  fold_convert (size_type_node, a),
			  build_int_cst (size_type_node,
					 HOST_WIDE_INT_1U << 63));
  val = copy_node (val);
  CONSTRUCTOR_ELTS (val) = vec_safe_copy (CONSTRUCTOR_ELTS (val));
  CONSTRUCTOR_ELT (val, 2)->value = enc;
  /* The record's elements are each a valid relocatable constant (the
     caller checks the whole record with initializer_constant_valid_p,
     which needs TREE_CONSTANT on the CONSTRUCTOR).  */
  bool cst = true;
  unsigned k;
  constructor_elt *ce;
  FOR_EACH_VEC_SAFE_ELT (CONSTRUCTOR_ELTS (val), k, ce)
    if (!initializer_constant_valid_p (ce->value, TREE_TYPE (ce->value)))
      cst = false;
  TREE_CONSTANT (val) = cst;
  TREE_STATIC (val) = cst;
  return val;
}

static tree
parse_initializer_1 (tree type, bool is_static)
{
  location_t loc = loc0 ();
  tree val;
  if (is_p ("{"))
    val = parse_brace_init (type, is_static);
  else
    {
      ex e = parse_assign ();
      return init_from_expr (e, type, is_static, loc);
    }
  return init_finish (val, is_static, loc);
}

/* The initializer of TYPE from the parsed expression E (no braces).  */
static tree
init_from_expr (ex e, tree type, bool is_static, location_t loc)
{
  tree val;
    {
      if (bad (e))
	return error_mark_node;
      if (TREE_CODE (type) == ARRAY_TYPE)
	{
	  if (e.t && TREE_CODE (e.t) == STRING_CST
	      && INTEGRAL_TYPE_P (TREE_TYPE (type))
	      && TYPE_PRECISION (TREE_TYPE (type))
		 == TYPE_PRECISION (TREE_TYPE (TREE_TYPE (e.t))))
	    return string_init (type, e, loc);
	  sorry_at (loc, "array initializer must be a brace list or string");
	  return error_mark_node;
	}
      if (is_static && e.t && VAR_P (e.t) && DECL_ARTIFICIAL (e.t)
	  && TYPE_MAIN_VARIANT (TREE_TYPE (e.t)) == TYPE_MAIN_VARIANT (type))
	for (unsigned k = 0; k + 1 < vec_safe_length (cl_raw_inits); k += 2)
	  if ((*cl_raw_inits)[k] == e.t)
	    return (*cl_raw_inits)[k + 1];
      e = convert_to (e, type, loc);
      if (bad (e))
	return error_mark_node;
      val = value (e);
    }
  return init_finish (val, is_static, loc);
}

/* Checks common to every initializer value.  */
static tree
init_finish (tree val, bool is_static, location_t loc)
{
  if (is_static && !error_p (val))
    val = static_size_sym_init (val);
  if (is_static && !error_p (val)
      && !initializer_constant_valid_p (val, TREE_TYPE (val)))
    {
      error_at (loc, "initializer element is not constant");
      return error_mark_node;
    }
  if (TREE_CODE (val) == CONSTRUCTOR && TREE_CONSTANT (val))
    TREE_STATIC (val) = 1;
  return val;
}

/* A static struct whose flexible array member is initialized (GNU C,
   the kernel's init_mm.cpu_bitmap): the variable's size includes the
   elements, as in GCC's C front end (add_flexible_array_elts_to_size), so
   its bounds cover them.  An automatic one is an error, as in GCC.  */
static bool
fam_init_size (tree var, tree init, location_t loc)
{
  tree type = TREE_TYPE (var);
  if (TREE_CODE (type) != RECORD_TYPE || TREE_CODE (init) != CONSTRUCTOR
      || vec_safe_is_empty (CONSTRUCTOR_ELTS (init)))
    return true;
  constructor_elt *ce = &CONSTRUCTOR_ELTS (init)->last ();
  tree f = ce->index;
  if (!f || TREE_CODE (f) != FIELD_DECL || !last_field_p (f)
      || TREE_CODE (TREE_TYPE (f)) != ARRAY_TYPE
      || TYPE_SIZE_UNIT (TREE_TYPE (f))
      || TREE_CODE (ce->value) != CONSTRUCTOR
      || vec_safe_is_empty (CONSTRUCTOR_ELTS (ce->value)))
    return true;
  if (!TREE_STATIC (var))
    {
      error_at (loc, "non-static initialization of a flexible array member");
      return false;
    }
  unsigned HOST_WIDE_INT n = 0, i;
  constructor_elt *e;
  FOR_EACH_VEC_SAFE_ELT (CONSTRUCTOR_ELTS (ce->value), i, e)
    {
      tree ix = e->index;
      if (ix && TREE_CODE (ix) == RANGE_EXPR)
	ix = TREE_OPERAND (ix, 1);
      unsigned HOST_WIDE_INT k = ix && tree_fits_uhwi_p (ix)
				 ? tree_to_uhwi (ix) : i;
      n = MAX (n, k + 1);
    }
  /* The initializer's array gets its element count, as GCC's
     complete_flexible_array_elts does (varasm sizes the element by its
     type: an array of unknown size made it pad by -17 bytes).  */
  TREE_TYPE (ce->value) = build_array_type_nelts (TREE_TYPE (TREE_TYPE (f)),
						  n);
  tree esz = TYPE_SIZE_UNIT (TREE_TYPE (TREE_TYPE (f)));
  tree extra = size_binop (MULT_EXPR, esz, size_int (n));
  tree end = size_binop (PLUS_EXPR, byte_position (f), extra);
  if (tree_int_cst_lt (DECL_SIZE_UNIT (var), end))
    {
      DECL_SIZE_UNIT (var) = end;
      DECL_SIZE (var) = size_binop (MULT_EXPR, fold_convert (bitsizetype, end),
				    bitsize_int (BITS_PER_UNIT));
    }
  return true;
}

/* Complete an incomplete array type from its initializer.  */
static tree
complete_array_from_init (tree type, tree init)
{
  if (TREE_CODE (type) != ARRAY_TYPE || TYPE_DOMAIN (type)
      || error_p (init))
    return type;
  HOST_WIDE_INT n;
  if (TREE_CODE (init) == STRING_CST)
    n = TREE_STRING_LENGTH (init)
	/ int_size_in_bytes (TREE_TYPE (TREE_TYPE (init)));
  else if (TREE_CODE (init) == CONSTRUCTOR)
    {
      /* Highest index + 1 (designators may leave gaps; elements are kept
	 in index order).  */
      n = 0;
      if (CONSTRUCTOR_NELTS (init))
	n = tree_to_shwi (CONSTRUCTOR_ELTS (init)->last ().index) + 1;
    }
  else
    return type;
  tree nt = build_array_type (TREE_TYPE (type),
			      build_index_type (size_int (n - 1)));
  TREE_TYPE (init) = nt;
  return nt;
}

/* Managed pointer slots inside an initializer (task #1 (b)).  The parsed
   initializer holds value records at slot positions; memory needs the
   8-byte address there and the bounds in the shadow region.  For static
   storage the address stays in the initializer and (slot, base, size) is
   queued for registration at startup (section tzd_shinit, read by libc2d).
   For automatic storage the slot is zeroed in the initializer and a store
   (address + shadow entry) is appended to STMTS.  REF is the reference to
   the subobject being initialized.  */
static tree
lower_slot_init (tree init, tree type, tree ref, bool is_static, tree *stmts,
		 location_t loc)
{
  if (error_p (init))
    return init;
  if (const char *const *hl = (is_static && !flag_tzd_kernel)
				? fnptr_holds_list (ref) : NULL)
    {
      /* A static `#pragma tzd fnptr_holds` member set to a static object
	 cast to the function type (readline's { ISKMAP, (rl_command_func_t *)
	 emacs_meta_keymap }): register it with the object's bounds and type
	 at startup (section tzd_shtyped, task #70).  */
      tree x = init;
      STRIP_NOPS (x);
      if (TREE_CODE (x) == ADDR_EXPR
	  && TREE_CODE (TREE_OPERAND (x, 0)) != FUNCTION_DECL)
	{
	  tree obj = get_base_address (TREE_OPERAND (x, 0));
	  tree pt = TREE_TYPE (TREE_OPERAND (x, 0));
	  if (TREE_CODE (pt) == ARRAY_TYPE)
	    pt = TREE_TYPE (pt);
	  if (!obj || !VAR_P (obj) || !DECL_SIZE_UNIT (obj)
	      || !fnptr_holds_allows (hl, pt))
	    {
	      error_at (loc, "initializer of a %<#pragma tzd fnptr_holds%> "
			"member must be a function or a whole object of a "
			"listed type");
	      return error_mark_node;
	    }
	  mark_addressable_tree (ref);
	  vec_safe_push (shtyped_list, build_fold_addr_expr_loc (loc, ref));
	  vec_safe_push (shtyped_list, build_fold_addr_expr_loc (loc, obj));
	  vec_safe_push (shtyped_list, DECL_SIZE_UNIT (obj));
	  vec_safe_push (shtyped_list,
			 build_int_cstu (size_type_node, fnptr_holds_id (pt)));
	}
      return init;
    }
  if (TZD_MSLOT_P (type))
    {
      if (!is_static)
	{
	  append_to_statement_list_force (slot_store (ref, init, loc), stmts);
	  return build_int_cst (type, 0);
	}
      if (TREE_CODE (init) != CONSTRUCTOR || CONSTRUCTOR_NELTS (init) != 3)
	{
	  error_at (loc, "initializer element is not constant");
	  return error_mark_node;
	}
      tree a = CONSTRUCTOR_ELT (init, 0)->value;
      tree b = CONSTRUCTOR_ELT (init, 1)->value;
      tree sz = CONSTRUCTOR_ELT (init, 2)->value;
      /* Only a null slot needs no entry; an integer constant cast to a
	 pointer is registered with no allocation (base 0) like at run time.  */
      if (!integer_zerop (b) || !integer_zerop (a))
	{
	  mark_addressable_tree (ref);
	  vec_safe_push (shinit_list, build_fold_addr_expr_loc (loc, ref));
	  vec_safe_push (shinit_list, b);
	  vec_safe_push (shinit_list, sz);
	}
      return fold_convert (type, a);
    }
  if (TREE_CODE (init) != CONSTRUCTOR
      || !(has_slots_p (type) || (is_static && has_fnholds_p (type))))
    return init;
  unsigned i;
  constructor_elt *ce;
  FOR_EACH_VEC_SAFE_ELT (CONSTRUCTOR_ELTS (init), i, ce)
    {
      tree sub, st;
      if (TREE_CODE (type) == ARRAY_TYPE)
	{
	  st = TREE_TYPE (type);
	  sub = build4 (ARRAY_REF, st, ref, ce->index, NULL_TREE, NULL_TREE);
	}
      else if (ce->index && TREE_CODE (ce->index) == FIELD_DECL)
	{
	  st = TREE_TYPE (ce->index);
	  sub = build3 (COMPONENT_REF, st, ref, ce->index, NULL_TREE);
	}
      else
	continue;
      ce->value = lower_slot_init (ce->value, st, sub, is_static, stmts, loc);
      if (error_p (ce->value))
	return error_mark_node;
    }
  /* Values changed: recompute the constant flags.  */
  bool cst = true;
  FOR_EACH_VEC_SAFE_ELT (CONSTRUCTOR_ELTS (init), i, ce)
    if (!TREE_CONSTANT (ce->value))
      cst = false;
  TREE_CONSTANT (init) = cst;
  if (cst)
    TREE_STATIC (init) = 1;
  return init;
}

/* Emit the static pointer registrations of this translation unit:
   an array of {slot, base, size} in section tzd_shinit.  libc2d walks
   __start_tzd_shinit .. __stop_tzd_shinit before main.  */
/* Does T refer to an object or function of this unit that was not output
   (removed as unused, as C would), or to one in a section the linker
   discards (.discard.*)?  Its registration must go too.  */
static tree
shinit_dead_ref (tree *tp, int *walk_subtrees, void *)
{
  tree t = *tp;
  if (TYPE_P (t))
    {
      *walk_subtrees = 0;
      return NULL_TREE;
    }
  if ((VAR_P (t) && (TREE_STATIC (t) || DECL_EXTERNAL (t)))
      || TREE_CODE (t) == FUNCTION_DECL)
    {
      if (!DECL_EXTERNAL (t) && !TREE_ASM_WRITTEN (t))
	return t;
      if (DECL_SECTION_NAME (t)
	  && startswith (DECL_SECTION_NAME (t), ".discard"))
	return t;
    }
  return NULL_TREE;
}

/* Called after the unit is compiled (post_compilation_parsing_cleanups),
   when it is known which objects were output.  */
void
tzd_emit_shinit_late (void)
{
  if (seen_error ())
    return;
  unsigned n = vec_safe_length (shinit_list) / 3, k = 0;
  for (unsigned i = 0; i < n; i++)
    {
      tree e[3] = { (*shinit_list)[3 * i], (*shinit_list)[3 * i + 1],
		    (*shinit_list)[3 * i + 2] };
      bool dead = false;
      for (tree x : e)
	if (walk_tree_without_duplicates (&x, shinit_dead_ref, NULL))
	  dead = true;
      if (dead)
	continue;
      for (unsigned j = 0; j < 3; j++)
	(*shinit_list)[3 * k + j] = e[j];
      k++;
    }
  vec_safe_truncate (shinit_list, 3 * k);
  emit_shinit ();
  /* fnptr_holds registrations (task #70): drop any whose slot or object
     was not output.  */
  unsigned tn = vec_safe_length (shtyped_list) / 4, tk = 0;
  for (unsigned i = 0; i < tn; i++)
    {
      tree a = (*shtyped_list)[4 * i], b = (*shtyped_list)[4 * i + 1];
      tree xa = a, xb = b;
      if (walk_tree_without_duplicates (&xa, shinit_dead_ref, NULL)
	  || walk_tree_without_duplicates (&xb, shinit_dead_ref, NULL))
	continue;
      for (unsigned j = 0; j < 4; j++)
	(*shtyped_list)[4 * tk + j] = (*shtyped_list)[4 * i + j];
      tk++;
    }
  vec_safe_truncate (shtyped_list, 4 * tk);
  emit_shtyped ();
  /* Function-interface registrations from static dispatch tables (task #61):
     drop any whose function was not output, then emit section tzd_fninit.  */
  unsigned fn = vec_safe_length (fninit_list) / 2, fk = 0;
  for (unsigned i = 0; i < fn; i++)
    {
      tree a = (*fninit_list)[2 * i], b = (*fninit_list)[2 * i + 1];
      tree x = a;
      if (walk_tree_without_duplicates (&x, shinit_dead_ref, NULL))
	continue;
      (*fninit_list)[2 * fk] = a;
      (*fninit_list)[2 * fk + 1] = b;
      fk++;
    }
  vec_safe_truncate (fninit_list, 2 * fk);
  emit_fninit ();
}

static void
emit_shinit (void)
{
  unsigned n = vec_safe_length (shinit_list) / 3;
  if (n == 0)
    return;
  tree et = tzd_mrec_type (void_type_node);   /* {ptr, ptr, size_t} */
  tree at = build_array_type (et, build_index_type (size_int (n - 1)));
  vec<constructor_elt, va_gc> *v = NULL;
  for (unsigned i = 0; i < n; i++)
    {
      vec<constructor_elt, va_gc> *e = NULL;
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 0),
			      fold_convert (TREE_TYPE (mp_field (et, 0)),
					    (*shinit_list)[3 * i]));
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 1),
			      fold_convert (ptr_type_node,
					    (*shinit_list)[3 * i + 1]));
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 2),
			      fold_convert (size_type_node,
					    (*shinit_list)[3 * i + 2]));
      tree c = build_constructor (et, e);
      TREE_CONSTANT (c) = TREE_STATIC (c) = 1;
      CONSTRUCTOR_APPEND_ELT (v, size_int (i), c);
    }
  tree init = build_constructor (at, v);
  TREE_CONSTANT (init) = TREE_STATIC (init) = 1;
  tree d = build_decl (BUILTINS_LOCATION, VAR_DECL,
		       get_identifier ("__tzd_shinit_tab"), at);
  TREE_STATIC (d) = 1;
  TREE_PUBLIC (d) = 0;
  DECL_ARTIFICIAL (d) = 1;
  TREE_USED (d) = 1;
  DECL_PRESERVE_P (d) = 1;
  /* Exactly 8-aligned: the linker concatenates every unit's table and
     libc2d walks them as one array (no padding between them).  */
  SET_DECL_ALIGN (d, 64);
  DECL_USER_ALIGN (d) = 1;
  DECL_INITIAL (d) = init;
  set_decl_section_name (d, "tzd_shinit");
  /* The unit is already compiled: output it directly.  */
  make_decl_rtl (d);
  assemble_variable (d, 1, 1, 0);
}

/* Like emit_shinit, for function-interface registrations (task #61): an
   array of {fn, 0, id} in the 24-byte shadow-record layout (same alignment
   and zero-padding behaviour across units), in section tzd_fninit.  libc2d
   walks it at startup and calls tzd_fn_reg; zero (padding) entries are
   skipped.  The id goes in the third field (a size_t holds the 64-bit id;
   this table is read only by tzd_fn_init_static, so the shinit size-symbol
   convention does not apply).  */
/* Section tzd_shtyped: {slot, base, size, id} words (task #70).  */
static void
emit_shtyped (void)
{
  unsigned n = vec_safe_length (shtyped_list);
  if (n == 0)
    return;
  tree at = build_array_type (ptr_type_node, build_index_type (size_int (n - 1)));
  vec<constructor_elt, va_gc> *v = NULL;
  for (unsigned i = 0; i < n; i++)
    CONSTRUCTOR_APPEND_ELT (v, size_int (i),
			    fold_convert (ptr_type_node, (*shtyped_list)[i]));
  tree init = build_constructor (at, v);
  TREE_CONSTANT (init) = TREE_STATIC (init) = 1;
  tree d = build_decl (BUILTINS_LOCATION, VAR_DECL,
		       get_identifier ("__tzd_shtyped_tab"), at);
  TREE_STATIC (d) = 1;
  TREE_PUBLIC (d) = 0;
  DECL_ARTIFICIAL (d) = 1;
  TREE_USED (d) = 1;
  DECL_PRESERVE_P (d) = 1;
  SET_DECL_ALIGN (d, 64);
  DECL_USER_ALIGN (d) = 1;
  DECL_INITIAL (d) = init;
  set_decl_section_name (d, "tzd_shtyped");
  make_decl_rtl (d);
  assemble_variable (d, 1, 1, 0);
}

static void
emit_fninit (void)
{
  unsigned n = vec_safe_length (fninit_list) / 2;
  if (n == 0)
    return;
  tree et = tzd_mrec_type (void_type_node);   /* {ptr, ptr, size_t} */
  tree at = build_array_type (et, build_index_type (size_int (n - 1)));
  vec<constructor_elt, va_gc> *v = NULL;
  for (unsigned i = 0; i < n; i++)
    {
      vec<constructor_elt, va_gc> *e = NULL;
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 0),
			      fold_convert (TREE_TYPE (mp_field (et, 0)),
					    (*fninit_list)[2 * i]));
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 1),
			      fold_convert (ptr_type_node, null_pointer_node));
      CONSTRUCTOR_APPEND_ELT (e, mp_field (et, 2),
			      fold_convert (size_type_node,
					    (*fninit_list)[2 * i + 1]));
      tree c = build_constructor (et, e);
      TREE_CONSTANT (c) = TREE_STATIC (c) = 1;
      CONSTRUCTOR_APPEND_ELT (v, size_int (i), c);
    }
  tree init = build_constructor (at, v);
  TREE_CONSTANT (init) = TREE_STATIC (init) = 1;
  tree d = build_decl (BUILTINS_LOCATION, VAR_DECL,
		       get_identifier ("__tzd_fninit_tab"), at);
  TREE_STATIC (d) = 1;
  TREE_PUBLIC (d) = 0;
  DECL_ARTIFICIAL (d) = 1;
  TREE_USED (d) = 1;
  DECL_PRESERVE_P (d) = 1;
  SET_DECL_ALIGN (d, 64);
  DECL_USER_ALIGN (d) = 1;
  DECL_INITIAL (d) = init;
  set_decl_section_name (d, "tzd_fninit");
  make_decl_rtl (d);
  assemble_variable (d, 1, 1, 0);
}

/* ------------------------------------------------------------------ */
/* Declarations.  */

static GTY(()) vec<tree, va_gc> *file_vars;

static tree
find_or_make_fndecl (tree id, tree type, location_t loc, dspec &ds,
		     tree asm_name)
{
  tree old = tzd_lookup (id);
  tree d;
  if (native_name_p (id))
    type = native_fn_type (type);
  /* A libc name the program replaces in Managed C (task #59): force the
     managed ABI at every site, overriding the system-header (raw) rule, so
     the body and its callers agree.  */
  bool mgd_override = managed_override_name_p (id);
  if (mgd_override)
    type = managed_fn_type (type);
  if (old && TREE_CODE (old) == FUNCTION_DECL)
    {
      d = old;
      /* First declared in a project header (gnulib's stdio.h and string.h
	 declare `free` and `memchr` for their attributes), then in the
	 system header: it is the host's function (task #71).  Not when
	 this unit already defines it.  */
      if (!DECL_IN_SYSTEM_HEADER (d) && in_system_header_at (loc)
	  && !DECL_SAVED_TREE (d) && !TREE_STATIC (d) && !mgd_override)
	DECL_SOURCE_LOCATION (d) = loc;   /* (what DECL_IN_SYSTEM_HEADER reads) */
      /* A host-libc function declared in a system header keeps its native
	 type even if user code redeclares it (raw-C boundary) -- unless it
	 is a program override, which is Managed C everywhere.  */
      if ((prototype_p (type) || !prototype_p (fn_lang_type (old)))
	  && (mgd_override
	      || !(DECL_IN_SYSTEM_HEADER (old) && prototype_p (TREE_TYPE (old)))))
	sv_set_fn_type (d, type);
    }
  else
    {
      d = build_decl (loc, FUNCTION_DECL, id, type);
      DECL_EXTERNAL (d) = 1;
      TREE_PUBLIC (d) = ds.st != ST_STATIC;
      DECL_CONTEXT (d) = NULL_TREE;
      if (!DECL_IN_SYSTEM_HEADER (d) || mgd_override)
	sv_set_fn_type (d, type);
      pushdecl (d);
    }
  if (TREE_PUBLIC (d) && !asm_name && sv_fntype_p (fn_lang_type (d))
      && !DECL_ASSEMBLER_NAME_SET_P (d))
    {
      /* Other ABI than C: a caller compiled without cc1tzd must not link.  */
      char *nm = ACONCAT ((IDENTIFIER_POINTER (id), "__tzd_byval", NULL));
      set_user_assembler_name (d, nm);
    }
  if (ds.inl)
    DECL_DECLARED_INLINE_P (d) = 1;
  /* C99 6.7.4: with every file-scope declaration of the function `inline`
     and none `extern`, its definition is an inline definition (no external
     definition in this unit: gnulib's `inline` functions in headers,
     task #71).  DECL_LANG_FLAG_0: some declaration was not inline or was
     extern; DECL_LANG_FLAG_1: the body was taken as an inline definition
     (it must be emitted after all if such a declaration comes later).  */
  if (!(ds.inl && ds.st != ST_EXTERN))
    {
      DECL_LANG_FLAG_0 (d) = 1;
      if (DECL_LANG_FLAG_1 (d))
	{
	  DECL_LANG_FLAG_1 (d) = 0;
	  DECL_EXTERNAL (d) = 0;
	}
    }
  if (asm_name)
    set_user_assembler_name (d, TREE_STRING_POINTER (asm_name));
  else if (TREE_PUBLIC (d) && DECL_CONTEXT (d) == NULL_TREE
	   && strcmp (IDENTIFIER_POINTER (id), "main") == 0)
    /* The user's main has managed argv/envp; libc2d's native main (main.c)
       wraps the C runtime's raw ones and calls this.  */
    set_user_assembler_name (d, "__tzd_main");
  return d;
}

static bool gnu89_extern_inline;

/* Does ID appear after '&' in the current function body?  */
static bool
addr_taken_p (tree id)
{
  if (cur_fn == NULL)
    return true;
  for (unsigned i = 0; i < cur_fn->addr_taken.length (); i++)
    if (cur_fn->addr_taken[i] == id)
      return true;
  return false;
}

static void
parse_function_body (tree fndecl, fparams *fp, location_t loc)
{
  tree fntype = TREE_TYPE (fndecl);
  tree rtype = TREE_TYPE (fntype);

  /* gnu89 "extern inline" definition: inline only, never emitted (Apache
     builds with -fgnu89-inline; os.h uses it).  */
  DECL_EXTERNAL (fndecl) = gnu89_extern_inline;
  gnu89_extern_inline = false;
  if (!flag_gnu89_inline && DECL_DECLARED_INLINE_P (fndecl)
      && TREE_PUBLIC (fndecl) && !DECL_LANG_FLAG_0 (fndecl)
      && !DECL_EXTERNAL (fndecl))
    {
      DECL_EXTERNAL (fndecl) = 1;
      DECL_LANG_FLAG_1 (fndecl) = 1;
    }
  TREE_STATIC (fndecl) = 1;
  /* Declared in a system header (libc2d's own functions): a delegate
     (task #79).  Read before the definition's location replaces it.  */
  bool delegate = DECL_IN_SYSTEM_HEADER (fndecl) || flag_tzd_delegate_unit;
  DECL_SOURCE_LOCATION (fndecl) = loc;

  tree res = build_decl (loc, RESULT_DECL, NULL_TREE, rtype);
  DECL_ARTIFICIAL (res) = 1;
  DECL_IGNORED_P (res) = 1;
  DECL_CONTEXT (res) = fndecl;
  DECL_RESULT (fndecl) = res;

  fn_ctx ctx;
  memset (&ctx, 0, sizeof ctx);
  ctx.decl = fndecl;
  ctx.delegate = delegate;
  fn_ctx *saved_fn = cur_fn;
  cur_fn = &ctx;
  current_function_decl = fndecl;
  /* The function's struct function exists while its body is parsed, as
     in the C front end: GCC's folders look at cfun (with none,
     __builtin_constant_p of a variable folds to 0 at once instead of
     after optimization, and the kernel's statically_true fails).  */
  struct function *saved_cfun = cfun;
  allocate_struct_function (fndecl, false);

  /* Pointer variables whose address is never taken stay value records
     (no shadow traffic); the others live in memory as 8-byte slots.  The
     body's tokens are scanned for '&' NAME up front (conservative: any
     '&' followed by the name, through parentheses).  */
  {
    int depth = 0;
    for (unsigned k = tpos; k < toks.length (); k++)
      {
	const tzd_token &tk = toks[k];
	if (tk.kind == TZD_PUNCT && strcmp (tk.text, "{") == 0)
	  depth++;
	else if (tk.kind == TZD_PUNCT && strcmp (tk.text, "}") == 0)
	  {
	    if (--depth <= 0)
	      break;
	  }
	else if (tk.kind == TZD_PUNCT
		 && (strcmp (tk.text, "&") == 0 || strcmp (tk.text, "&&") == 0))
	  {
	    unsigned j = k + 1;
	    while (j < toks.length () && toks[j].kind == TZD_PUNCT
		   && strcmp (toks[j].text, "(") == 0)
	      j++;
	    if (j < toks.length () && toks[j].kind == TZD_IDENT)
	      {
		/* `&p->f` / `&p[i]` take the address of what p points to,
		   not of p itself.  Over-marking is safe (p just lives in
		   memory); under-marking is caught by address_of.  */
		/* Only the parentheses that were opened right after the
		   `&` close here: in `f (&p)->x` the `)` ends the call, and
		   `->x` applies to its result, not to p.  */
		unsigned m = j + 1, opened = j - (k + 1);
		while (opened && m < toks.length () && toks[m].kind == TZD_PUNCT
		       && strcmp (toks[m].text, ")") == 0)
		  m++, opened--;
		bool through = (m < toks.length () && toks[m].kind == TZD_PUNCT
				&& (strcmp (toks[m].text, "->") == 0
				    || strcmp (toks[m].text, "[") == 0));
		if (!through)
		  ctx.addr_taken.safe_push (get_identifier (toks[j].text));
	      }
	  }
	else if (tk.kind == TZD_EOF)
	  break;
      }
  }

  tzd_push_scope ();
  tree parms = NULL_TREE, *tail = &parms;
  vec<tree> spill = vNULL;   /* (slot var, parm) pairs */
  ctx.lang_ret = TREE_TYPE (fn_lang_type (fndecl));
  if (sv_type_p (ctx.lang_ret))
    {
      /* Lowered result (sv): the caller's result object.  */
      tree p = build_decl (loc, PARM_DECL, get_identifier ("__tzd_result"),
			   build_pointer_type (ctx.lang_ret));
      DECL_ARG_TYPE (p) = TREE_TYPE (p);
      DECL_CONTEXT (p) = fndecl;
      DECL_ARTIFICIAL (p) = 1;
      TREE_USED (p) = 1;
      *tail = p;
      tail = &DECL_CHAIN (p);
      ctx.sv_ret = p;
    }
  /* A native function (-ftzd-native) receives raw pointers: the body's
     variable is made from each through from_native (spill).  */
  tree nat_arg = (native_name_p (DECL_NAME (fndecl))
		  && !sv_fntype_p (fn_lang_type (fndecl))
		  && prototype_p (TREE_TYPE (fndecl)))
		 ? TYPE_ARG_TYPES (TREE_TYPE (fndecl)) : NULL_TREE;
  if (fp)
    for (unsigned i = 0; i < fp->list.length (); i++)
      {
	param_info &pi = fp->list[i];
	tree nat_t = NULL_TREE;
	if (nat_arg && TREE_VALUE (nat_arg) != void_type_node)
	  {
	    nat_t = TREE_VALUE (nat_arg);
	    nat_arg = TREE_CHAIN (nat_arg);
	  }
	if (pi.id == NULL_TREE)
	  {
	    error_at (pi.loc, "parameter name omitted");
	    continue;
	  }
	if (nat_t && TZD_MPTR_P (pi.type) && raw_data_ptr_p (nat_t))
	  {
	    tree p = build_decl (pi.loc, PARM_DECL, NULL_TREE, nat_t);
	    DECL_ARG_TYPE (p) = nat_t;
	    DECL_CONTEXT (p) = fndecl;
	    DECL_ARTIFICIAL (p) = 1;
	    TREE_USED (p) = 1;
	    *tail = p;
	    tail = &DECL_CHAIN (p);
	    bool at = addr_taken_p (pi.id);
	    tree v = build_decl (pi.loc, VAR_DECL, pi.id,
				 at ? pi.type : vtype (pi.type));
	    DECL_CONTEXT (v) = fndecl;
	    TREE_USED (v) = 1;
	    if (at)
	      TREE_ADDRESSABLE (v) = 1;
	    spill.safe_push (v);
	    spill.safe_push (p);
	    pushdecl (v);
	    continue;
	  }
	if (sv_type_p (pi.type))
	  {
	    /* Lowered parameter (sv): a pointer to the caller's copy; the
	       body's variable is a bounds copy of it.  */
	    tree p = build_decl (pi.loc, PARM_DECL, NULL_TREE,
				 build_pointer_type (pi.type));
	    DECL_ARG_TYPE (p) = TREE_TYPE (p);
	    DECL_CONTEXT (p) = fndecl;
	    DECL_ARTIFICIAL (p) = 1;
	    TREE_USED (p) = 1;
	    *tail = p;
	    tail = &DECL_CHAIN (p);
	    tree v = build_decl (pi.loc, VAR_DECL, pi.id, pi.type);
	    DECL_CONTEXT (v) = fndecl;
	    TREE_USED (v) = 1;
	    TREE_ADDRESSABLE (v) = 1;
	    spill.safe_push (v);
	    spill.safe_push (p);
	    pushdecl (v);
	    continue;
	  }
	tree pty = vtype (pi.type);
	tree p = build_decl (pi.loc, PARM_DECL, pi.id, pty);
	DECL_ARG_TYPE (p) = fp->knr ? knr_promoted (pty) : pty;
	DECL_CONTEXT (p) = fndecl;
	TREE_USED (p) = 1;
	*tail = p;
	tail = &DECL_CHAIN (p);
	if (TZD_MPTR_P (pi.type) && addr_taken_p (pi.id))
	  {
	    /* &param: the callee keeps it in a memory slot.  */
	    tree v = build_decl (pi.loc, VAR_DECL, pi.id, pi.type);
	    DECL_CONTEXT (v) = fndecl;
	    TREE_USED (v) = 1;
	    TREE_ADDRESSABLE (v) = 1;
	    spill.safe_push (v);
	    spill.safe_push (p);
	    pushdecl (v);
	  }
	else
	  pushdecl (p);
      }
  DECL_ARGUMENTS (fndecl) = parms;

  tree body = NULL_TREE;
  parse_compound (&body, true, false, &spill);
  spill.release ();
  tzd_pop_scope ();

  DECL_SAVED_TREE (fndecl) = body;
  location_t end = toks[tpos > 0 ? tpos - 1 : 0].loc;

  struct function *fn_cfun = cfun;
  set_cfun (saved_cfun);
  current_function_decl = saved_fn ? saved_fn->decl : NULL_TREE;
  cur_fn = saved_fn;
  ctx.break_labels.release ();
  ctx.continue_labels.release ();
  ctx.labels.release ();
  ctx.switch_types.release ();
  ctx.block_chains.release ();
  ctx.addr_taken.release ();
  ctx.local_labels.release ();

  if (seen_error ())
    return;
  fn_cfun->function_end_locus = end;
  if (ctx.label_in_static)
    fn_cfun->has_forced_label_in_static = 1;
  cgraph_node::finalize_function (fndecl, true);
}

/* TYPE with every managed pointer level made raw (a host object's type).  */
static tree
deep_raw_type (tree t)
{
  if (TZD_MPTR_P (t))
    return build_qualified_type (build_pointer_type
				   (deep_raw_type (TZD_MPTR_TARGET (t))),
				 TYPE_QUALS (t));
  return t;
}

/* Host-libc objects that programs commonly declare themselves
   (`extern char **environ;` in PHP 3, no system header declaring it):
   such an extern declaration names the host's object, so it gets the
   native type and the host rule (host_ptr_rules) applies to what is read
   from it.  Only names with a rule, only extern declarations.  */
static bool
host_object_name_p (tree id)
{
  static const char *const names[] = {
    "environ", "__environ", "optarg", "stdin", "stdout", "stderr",
    "__progname"
  };
  for (size_t i = 0; i < ARRAY_SIZE (names); i++)
    if (strcmp (IDENTIFIER_POINTER (id), names[i]) == 0)
      return host_ptr_rule_for (names[i]) != NULL;
  return false;
}

static tree
make_var (tree id, tree type, location_t loc, dspec &ds, bool file_scope)
{
  if (ds.st == ST_EXTERN && host_object_name_p (id))
    type = deep_raw_type (type);
  bool old_defined = false;   /* D is an earlier definition */
  tree d = NULL_TREE;
  if (file_scope)
    {
      tree old = tzd_lookup (id);
      if (old && TREE_CODE (old) == VAR_DECL)
	{
	  d = old;
	  old_defined = !DECL_EXTERNAL (old);
	  /* Host-libc objects (environ, optarg, ...) keep the native type.  */
	  if (COMPLETE_TYPE_P (type)
	      && !(DECL_IN_SYSTEM_HEADER (old) && COMPLETE_TYPE_P (TREE_TYPE (old))))
	    TREE_TYPE (d) = type;
	}
    }
  if (d == NULL_TREE)
    {
      if (!file_scope && ds.st != ST_STATIC && ds.st != ST_EXTERN
	  && TZD_MSLOT_P (type) && !addr_taken_p (id) && !force_slot
	  /* `T *volatile p` keeps its memory slot: the value record is
	     not volatile, so p could live in registers and lose a value
	     assigned between setjmp and longjmp (bash's parse_and_execute:
	     COMMAND *volatile command).  */
	  && !TYPE_VOLATILE (type))
	/* Local pointer whose address is never taken: a value record, no
	   memory slot (task #1 (b)).  */
	type = mrec (type);
      d = build_decl (loc, VAR_DECL, id, type);
      if (file_scope)
	{
	  TREE_STATIC (d) = 1;
	  TREE_PUBLIC (d) = ds.st != ST_STATIC;
	  vec_safe_push (file_vars, d);
	}
      else if (ds.st == ST_STATIC)
	{
	  TREE_STATIC (d) = 1;
	  DECL_CONTEXT (d) = current_function_decl;
	}
      else if (ds.st == ST_EXTERN)
	{
	  TREE_PUBLIC (d) = 1;
	}
      else
	DECL_CONTEXT (d) = current_function_decl;
      pushdecl (d);
    }
  if (ds.st == ST_EXTERN)
    {
      /* `extern T x;` after x's definition (the kernel's EXPORT_SYMBOL
	 repeats it) still refers to that definition, as in C.  */
      if (!old_defined)
	DECL_EXTERNAL (d) = 1;
    }
  else if (file_scope)
    DECL_EXTERNAL (d) = 0;
  /* The object's own qualifiers: in `const char *p` the const belongs to
     the pointee (the declaration specifiers), p itself is writable.  */
  tree ot = strip_array_types (TREE_TYPE (d));
  if (TYPE_READONLY (ot)
      || ((ds.quals & TYPE_QUAL_CONST) && !POINTER_TYPE_P (ot)
	  && !TZD_MPTR_P (ot)))
    TREE_READONLY (d) = 1;
  if (TYPE_VOLATILE (ot)
      || ((ds.quals & TYPE_QUAL_VOLATILE) && !POINTER_TYPE_P (ot)
	  && !TZD_MPTR_P (ot)))
    TREE_THIS_VOLATILE (d) = TREE_SIDE_EFFECTS (d) = 1;
  return d;
}

/* Type a K&R parameter receives from its caller (default promotions).  */
static tree
knr_promoted (tree t)
{
  if (INTEGRAL_TYPE_P (t) && !TZD_MPTR_P (t)
      && TYPE_PRECISION (t) < TYPE_PRECISION (integer_type_node))
    return integer_type_node;
  if (TYPE_MAIN_VARIANT (t) == float_type_node)
    return double_type_node;
  return t;
}

/* K&R definition: the parameter declarations between ')' and '{'.
   Undeclared parameters are int.  */
static void
parse_knr_decls (fparams *fp)
{
  while (!is_p ("{") && peek ().kind != TZD_EOF)
    {
      PROGRESS_GUARD;
      if (!decl_start_p ())
	{
	  error_at (loc0 (), "expected declaration or %<{%>");
	  skip_to_semi ();
	  continue;
	}
      dspec ds;
      parse_declspecs (ds);
      tree base = finish_declspecs (ds);
      for (;;)
	{
	  declarator d;
	  memset (&d, 0, sizeof d);
	  parse_declarator (d, false);
	  fparams *dummy;
	  tree t = apply_declarator (base, d, &dummy);
	  d.ops.release ();
	  if (!error_p (t))
	    {
	      if (va_list_p (t))
		t = build_pointer_type (TREE_TYPE (t));
	      else if (TREE_CODE (t) == ARRAY_TYPE)
		t = make_pointer (TREE_TYPE (t));
	      else if (TREE_CODE (t) == FUNCTION_TYPE)
		t = build_pointer_type (t);
	    }
	  bool found = false;
	  for (unsigned i = 0; i < fp->list.length (); i++)
	    if (fp->list[i].id == d.id)
	      {
		fp->list[i].type = t;
		found = true;
	      }
	  if (!found && d.id)
	    error_at (d.loc, "declaration for parameter %qE but no such "
		      "parameter", d.id);
	  if (!accept (","))
	    break;
	}
      expect (";");
    }
  for (unsigned i = 0; i < fp->list.length (); i++)
    if (fp->list[i].type == NULL_TREE)
      fp->list[i].type = integer_type_node;
}

/* Parse a declaration (after lookahead says it is one).  At file scope a
   function definition may follow the first declarator.  */
static void
parse_declaration (bool file_scope)
{
  attr_gnu_inline = false;
  check_attr_mode_used ();
  /* A `// @tzd` annotation right before the declaration (task #79).  */
  tzd_annot *an = peek ().annot;
  if (is_w ("_Static_assert"))
    {
      parse_static_assert ();
      return;
    }
  dspec ds;
  parse_declspecs (ds);
  struct sys_static_guard {
    bool on;
    ~sys_static_guard () { if (on) sys_static_depth--; }
  } ssg = { ds.st == ST_STATIC && native_region_p () };
  if (ssg.on)
    sys_static_depth++;
  tree base = finish_declspecs (ds);
  attr_layout spec_lay = take_attr_layout ();
  tree spec_pass = attr_decl_list;
  attr_decl_list = NULL_TREE;
  if (accept (";"))
    {
      if (spec_pass)
	error_at (attr_decl_loc, "attribute %qE does not apply here "
		  "(task #26)", TREE_PURPOSE (spec_pass));
      if (spec_lay.align || spec_lay.packed)
	error_at (spec_lay.loc, "attribute %<%s%> does not apply here "
		  "(task #26)", spec_lay.packed ? "packed" : "aligned");
      return;   /* struct/union/enum declaration only */
    }

  bool first = true;
  for (;;)
    {
      declarator d;
      memset (&d, 0, sizeof d);
      parse_declarator (d, false);
      fparams *fp;
      tree type = apply_declarator (base, d, &fp);
      if (ds.auto_type && !error_p (type))
	{
	  /* __auto_type x = e: x has the type of e after lvalue conversion
	     (GNU C).  Read e once for its type, then parse the declaration
	     normally with that type.  */
	  if (d.ops.length () || !is_p ("=") || file_scope)
	    {
	      error_at (d.loc, "%<__auto_type%> needs a plain identifier and "
			"an initializer");
	      type = error_mark_node;
	    }
	  else
	    {
	      unsigned save = tpos;
	      next ();
	      ex e = parse_assign ();
	      tpos = save;
	      if (bad (e))
		type = error_mark_node;
	      else
		{
		  tree et = e.type;
		  if (TREE_CODE (et) == ARRAY_TYPE)
		    et = tzd_mptr_type (TREE_TYPE (et));
		  et = TYPE_MAIN_VARIANT (tzd_lang_type (et));
		  type = ds.quals ? build_qualified_type (et, ds.quals) : et;
		}
	    }
	}
      if (error_p (type) || d.id == NULL_TREE)
	{
	  d.ops.release ();
	  skip_to_semi ();
	  return;
	}
      /* aligned/packed given in the specifiers or after this declarator
	 (task #26/#42).  packed only means something on a struct type.  */
      attr_layout lay = merge_attr_layout (spec_lay, take_attr_layout ());
      tree pass = chainon (copy_list (spec_pass), attr_decl_list);
      attr_decl_list = NULL_TREE;
      tree cleanup_fn = attr_cleanup_fn;
      location_t cleanup_loc = attr_cleanup_loc;
      attr_cleanup_fn = NULL_TREE;
      if (cleanup_fn && (file_scope || ds.st == ST_STATIC
			 || ds.st == ST_EXTERN || ds.st == ST_TYPEDEF
			 || TREE_CODE (type) == FUNCTION_TYPE))
	{
	  error_at (cleanup_loc, "attribute %<cleanup%> applies only to "
		    "automatic variables");
	  cleanup_fn = NULL_TREE;
	}
      if (pass && ds.st == ST_TYPEDEF)
	{
	  error_at (attr_decl_loc, "attribute %qE does not apply to a typedef "
		    "(task #26)", TREE_PURPOSE (pass));
	  pass = NULL_TREE;
	}
      if (lay.packed)
	error_at (lay.loc, "attribute %<packed%> applies only to struct "
		  "and union types and their members (task #26)");
      if (lay.transparent)
	{
	  if (ds.st == ST_TYPEDEF && TREE_CODE (type) == UNION_TYPE)
	    TYPE_TRANSPARENT_AGGR (TYPE_MAIN_VARIANT (type)) = 1;
	  else
	    error_at (lay.loc, "attribute %<transparent_union%> applies only "
		      "to unions");
	}
      if (lay.align && ds.st == ST_TYPEDEF)
	type = build_aligned_type (type, MAX (TYPE_ALIGN (type),
					      lay.align * BITS_PER_UNIT));
      if (lay.may_alias)
	/* On a typedef or a declaration: the declared type (as GCC does).  */
	type = build_type_attribute_variant
		 (type, tree_cons (get_identifier ("may_alias"), NULL_TREE,
				   TYPE_ATTRIBUTES (type)));

      if (an && !an->used && (!first || ds.st == ST_TYPEDEF
			      || TREE_CODE (type) == FUNCTION_TYPE))
	{
	  an->used = true;
	  error_at (an->loc, first ? "a %<@tzd%> annotation applies to a "
		    "variable definition here (task #79)"
		    : "an annotated declaration declares one variable "
		    "(task #79)");
	}
      if (ds.st == ST_TYPEDEF)
	{
	  tree td = build_decl (d.loc, TYPE_DECL, d.id, type);
	  if (RECORD_OR_UNION_TYPE_P (type) && TYPE_NAME (type) == NULL_TREE)
	    TYPE_NAME (type) = td;
	  pushdecl (td);
	}
      else if (TREE_CODE (type) == FUNCTION_TYPE)
	{
	  tree fndecl = find_or_make_fndecl (d.id, type, d.loc, ds,
					     d.asm_name);
	  if (lay.align)
	    {
	      SET_DECL_ALIGN (fndecl, MAX (DECL_ALIGN (fndecl),
					   lay.align * BITS_PER_UNIT));
	      DECL_USER_ALIGN (fndecl) = 1;
	    }
	  apply_attr_decl (fndecl, pass);
	  if (first && file_scope && fp && fp->knr && !is_p (";")
	      && !is_p (","))
	    parse_knr_decls (fp);
	  if (first && file_scope && is_p ("{"))
	    {
	      if (ds.st == ST_STATIC)
		TREE_PUBLIC (fndecl) = 0;
	      /* `extern inline` is an inline-only definition in gnu89 mode; in C99
		 mode it is the external definition of the function (gnulib's
		 streq.c, memeq.c, ...).  */
	      gnu89_extern_inline = ds.inl && ds.st == ST_EXTERN
				    && (flag_gnu89_inline || attr_gnu_inline);
	      attr_gnu_inline = false;
	      parse_function_body (fndecl, fp, d.loc);
	      d.ops.release ();
	      return;
	    }
	}
      else
	{
	  if (VOID_TYPE_P (type) && ds.st != ST_EXTERN)
	    {
	      error_at (d.loc, "variable %qE declared void", d.id);
	      d.ops.release ();
	      skip_to_semi ();
	      return;
	    }
	  force_slot = cleanup_fn != NULL_TREE;
	  tree var = make_var (d.id, type, d.loc, ds, file_scope);
	  force_slot = false;
	  if (lay.align)
	    {
	      SET_DECL_ALIGN (var, MAX (DECL_ALIGN (var),
					lay.align * BITS_PER_UNIT));
	      DECL_USER_ALIGN (var) = 1;
	    }
	  apply_attr_decl (var, pass);
	  if (an && !an->used)
	    {
	      an->used = true;
	      grp_apply_annot (an, var, d.loc, file_scope);
	    }
	  var_slot_align (var);
	  if (!file_scope && VAR_P (var) && !TREE_STATIC (var)
	      && !DECL_EXTERNAL (var) && TYPE_SIZE_UNIT (TREE_TYPE (var))
	      && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (var))) != INTEGER_CST)
	    {
	      /* A variable length array: its size is evaluated and its
		 storage allocated here (the gimplifier's DECL_EXPR).  */
	      if (cur_fn)
		add_stmt_tzd (build1 (DECL_EXPR, void_type_node, var));
	      else
		error_at (d.loc, "variable length array outside a function");
	    }
	  tree slot_stmts = NULL_TREE;
	  if (d.asm_name && ds.st == ST_REGISTER)
	    {
	      /* GNU register variable (`register unsigned long
		 current_stack_pointer asm("rsp")`): it names a machine
		 register, not memory.  */
	      if (TZD_MPTR_P (type))
		{
		  /* A pointer in a machine register is a raw address (the
		     kernel's call_on_stack: `register void *tos asm("r11")`
		     handed to asm): an assignment stores pointer(); the
		     bounds stay behind.  */
		  TREE_TYPE (var)
		    = build_qualified_type (build_pointer_type
					      (TZD_MPTR_TARGET (type)),
					    TYPE_QUALS (type));
		  relayout_decl (var);
		}
	      else if (has_slots_p (TREE_TYPE (var)))
		error_at (d.loc, "an object holding managed pointers cannot "
			  "live in a fixed register");
	      DECL_REGISTER (var) = 1;
	      DECL_HARD_REGISTER (var) = 1;
	      if (file_scope)
		{
		  /* As in C: neither static nor external storage (a
		     static would be "never written", i.e. constant 0).  */
		  TREE_PUBLIC (var) = 0;
		  DECL_EXTERNAL (var) = 0;
		  TREE_STATIC (var) = 0;
		}
	    }
	  if (d.asm_name)
	    set_user_assembler_name (var, TREE_STRING_POINTER (d.asm_name));
	  if (accept ("="))
	    {
	      bool st = TREE_STATIC (var) || file_scope;
	      tree init = parse_initializer (TREE_TYPE (var), st);
	      tree nt = complete_array_from_init (TREE_TYPE (var), init);
	      if (nt != TREE_TYPE (var))
		{
		  TREE_TYPE (var) = nt;
		  DECL_SIZE (var) = DECL_SIZE_UNIT (var) = NULL_TREE;
		  layout_decl (var, 0);
		}
	      if (!error_p (init)
		  && (has_slots_p (TREE_TYPE (var))
		      || (st && has_fnholds_p (TREE_TYPE (var)))))
		{
		  if (!st && TREE_CODE (init) != CONSTRUCTOR
		      && !TZD_MSLOT_P (TREE_TYPE (var)))
		    {
		      /* struct s a = b;  -- bytes and shadow entries.  */
		      slot_stmts = alloc_stmt_list ();
		      append_to_statement_list_force
			(aggregate_copy (var, init, d.loc), &slot_stmts);
		      init = NULL_TREE;
		    }
		  else
		    {
		      if (!st)
			slot_stmts = alloc_stmt_list ();
		      init = lower_slot_init (init, TREE_TYPE (var), var, st,
					      &slot_stmts, d.loc);
		      if (!st && TZD_MSLOT_P (TREE_TYPE (var)))
			init = NULL_TREE;   /* the store does it */
		    }
		}
	      if (init && !error_p (init) && !fam_init_size (var, init, d.loc))
		init = error_mark_node;
	      if (init && !error_p (init))
		{
		  DECL_INITIAL (var) = init;
		  if (ds.st == ST_EXTERN)
		    DECL_EXTERNAL (var) = 0;
		}
	    }
	  else if (!file_scope && !TREE_STATIC (var) && !DECL_EXTERNAL (var)
		   && TZD_MREC_P (TREE_TYPE (var)))
	    /* An automatic managed pointer without an initializer starts as
	       the null pointer: its bounds must not be whatever the stack
	       held before (reading it, or an asm output that keeps the old
	       bounds, would use them).  */
	    DECL_INITIAL (var) = build_constructor (TREE_TYPE (var), NULL);
	  if (!file_scope)
	    {
	      if (TREE_STATIC (var))
		rest_of_decl_compilation (var, 0, 0);
	      else if (!DECL_EXTERNAL (var))
		{
		  if (!COMPLETE_TYPE_P (TREE_TYPE (var)))
		    error_at (d.loc, "storage size of %qE isn%'t known", d.id);
		  add_stmt_tzd (build1 (DECL_EXPR, void_type_node, var));
		}
	      if (slot_stmts)
		add_stmt_tzd (slot_stmts);
	      if (cleanup_fn && !TREE_STATIC (var))
		push_cleanup_call (cleanup_fn, var, cleanup_loc);
	    }
	}
      d.ops.release ();
      first = false;
      if (!accept (","))
	break;
    }
  expect (";");
}

/* ------------------------------------------------------------------ */
/* cleanup attribute.  From the declaration on, the rest of the block is
   the body of a TRY_FINALLY_EXPR whose cleanup calls fn (&var): it runs
   however the block is left (fall-through, break, continue, return,
   goto), only when the declaration was reached, innermost first, and
   before the block wipes its locals.  */
struct cleanup_ent { tree *saved; tree *rest; tree call; };
static vec<cleanup_ent> cleanups;

static void
push_cleanup_call (tree fn, tree var, location_t loc)
{
  tree lt = fn_lang_type (fn);
  tree at = TYPE_ARG_TYPES (lt);
  if (!at || TREE_VALUE (at) == void_type_node
      || (TREE_CHAIN (at) && TREE_VALUE (TREE_CHAIN (at)) != void_type_node))
    {
      error_at (loc, "cleanup function must take one argument");
      return;
    }
  ex a = address_of (mk (var, TREE_TYPE (var), true), loc);
  ex c = convert_to (a, TREE_VALUE (at), loc);
  if (bad (c))
    return;
  TREE_USED (fn) = 1;
  cleanup_ent ce;
  ce.saved = cur_stmts;
  ce.rest = XNEW (tree);
  *ce.rest = alloc_stmt_list ();
  ce.call = build_call_expr_loc (loc, fn, 1, value (c));
  cleanups.safe_push (ce);
  cur_stmts = ce.rest;
}

/* Close the cleanups opened since START (the scope ends).  */
static void
unwind_cleanups (unsigned start, location_t loc)
{
  while (cleanups.length () > start)
    {
      cleanup_ent ce = cleanups.pop ();
      tree tf = build2_loc (loc, TRY_FINALLY_EXPR, void_type_node, *ce.rest,
			    ce.call);
      cur_stmts = ce.saved;
      add_stmt_tzd (tf);
      XDELETE (ce.rest);
    }
}

/* Whether V's destruction is left to the late pass (tzd-pass.cc): when
   optimizing, every automatic variable that is not volatile.  Whether V
   has stack bytes at all is only known after inlining and scalar
   replacement, and a wipe emitted here takes V's address, which forces a
   stack home just for the wipe (the kernel's ptep_get inlined in a loop:
   the 24-byte pointer parameter written out and wiped on every
   iteration, 90% of all wipes of an ssh login, task #69).  The late pass
   wipes every such variable that did end up in memory at the end of its
   storage -- the same place, the exit of its scope -- and before any
   return it can reach unwiped; it removes the bounds entries too unless
   the variable can have none (no pointer in its type and, by then, no
   address taken).  A variable kept in registers is cleared at the
   function's exit (-fzero-call-used-regs).
   An address taken here does not decide it: indexing a local array takes
   its address in this front end, and the optimizers often undo that.
   -O0 keeps every local on the stack, and a volatile one is kept in
   memory: those are wiped here, at the scope's exit.  */
static bool
register_only_p (tree v)
{
  return optimize > 0 && !TREE_THIS_VOLATILE (v)
	 && !TYPE_VOLATILE (strip_array_types (TREE_TYPE (v)))
	 /* A variable length array is wiped here, with its run-time size.  */
	 && TYPE_SIZE_UNIT (TREE_TYPE (v))
	 && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (v))) == INTEGER_CST;
}

/* Wipes of the automatic variables VARS of a scope that is not a
   compound statement (a for statement's declarations).  */
static tree
scope_wipe_stmts (tree vars, location_t end)
{
  tree wipes = NULL_TREE;
  for (tree v = vars; v; v = DECL_CHAIN (v))
    if (VAR_P (v) && !TREE_STATIC (v) && !DECL_EXTERNAL (v)
	&& !DECL_HARD_REGISTER (v) && !register_only_p (v)
	&& TYPE_SIZE_UNIT (TREE_TYPE (v))
	/* A variable length array is wiped with its run-time size.  */
	&& (TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (v))) != INTEGER_CST
	    || !integer_zerop (TYPE_SIZE_UNIT (TREE_TYPE (v)))))
      {
	init_shadow_decls ();
	TREE_ADDRESSABLE (v) = 1;
	tree w = build_call_expr_loc (end, tzd_sh_wipe_decl, 2,
				      build_fold_addr_expr_loc (end, v),
				      fold_convert (size_type_node,
						    TYPE_SIZE_UNIT
						      (TREE_TYPE (v))));
	if (!wipes)
	  wipes = alloc_stmt_list ();
	append_to_statement_list_force (w, &wipes);
      }
  return wipes;
}

/* ------------------------------------------------------------------ */
/* Statements.  */

static void parse_statement (void);

/* Parse STMT into a fresh statement list.  */
static tree
parse_sub_statement (void)
{
  tree list = alloc_stmt_list ();
  tree *saved = cur_stmts;
  cur_stmts = &list;
  sub_stmt_depth++;
  parse_statement ();
  sub_stmt_depth--;
  cur_stmts = saved;
  return list;
}

/* The registers are cleared at the function's exit, not at each scope's
   (TK 2026-11-17, task #69; was an asm zeroing 14 registers after every
   scope's wipes, which in an inlined body inside a loop forced every live
   value into an unwiped spill slot): the general registers by
   -fzero-call-used-regs=all-gpr (tzd-opts.cc), the vector registers by
   the late pass's call of libc2d's tzd_vregs_clear (tzd-pass.cc).  */
static GTY(()) tree tzd_vregs_clear_decl;

tree
tzd_vregs_clear_fn (void)
{
  if (!tzd_vregs_clear_decl)
    tzd_vregs_clear_decl
      = libd_decl ("tzd_vregs_clear",
		   build_function_type_list (void_type_node, NULL_TREE));
  return tzd_vregs_clear_decl;
}

static void init_shadow_decls (void);
tree
tzd_sh_wipe_fn (void)
{
  init_shadow_decls ();
  return tzd_sh_wipe_decl;
}

tree
tzd_sh_wipe_data_fn (void)
{
  init_shadow_decls ();
  return tzd_sh_wipe_data_decl;
}

static void
parse_compound (tree *body, bool fn_body, bool capture, vec<tree> *spill)
{
  location_t loc = loc0 ();
  if (!expect ("{"))
    return;
  unsigned cl_start = vec_safe_length (cl_wipes);
  unsigned cu_start = cleanups.length ();
  unsigned ll_start = cur_fn->local_labels.length ();
  compound_depth++;
  tree list = alloc_stmt_list ();
  tree *saved = cur_stmts;
  cur_stmts = &list;
  tzd_push_scope ();
  cur_fn->block_chains.safe_push (NULL_TREE);
  if (spill)
    /* Parameters whose address is taken: copy into their memory slots.  */
    for (unsigned i = 0; i + 1 < spill->length (); i += 2)
      {
	tree v = (*spill)[i], p = (*spill)[i + 1];
	if (RECORD_OR_UNION_TYPE_P (TREE_TYPE (v))
	    && !TZD_MREC_P (TREE_TYPE (v)))
	  /* Struct parameter passed by hidden pointer (sv).  */
	  add_stmt_tzd (aggregate_copy (v, build_simple_mem_ref (p), loc));
	else if (raw_data_ptr_p (TREE_TYPE (p)))
	  {
	    /* Raw pointer parameter of a native function.  */
	    tree w = whole (from_native (p, tzd_mptr_type (TREE_TYPE
							    (TREE_TYPE (p))),
					 loc));
	    if (TREE_ADDRESSABLE (v))
	      add_stmt_tzd (slot_store (v, w, loc));
	    else
	      add_stmt_tzd (build2 (MODIFY_EXPR, TREE_TYPE (v), v,
				    fold_build1 (VIEW_CONVERT_EXPR,
						 TREE_TYPE (v), w)));
	  }
	else
	  add_stmt_tzd (slot_store (v, p, loc));
      }

  while (!is_p ("}") && peek ().kind != TZD_EOF)
    {
      PROGRESS_GUARD;
      if (is_w ("__label__"))
	{
	  /* GNU local labels: names bound to fresh labels in this block
	     (so a macro can use the same label name twice).  */
	  next ();
	  while (peek ().kind == TZD_IDENT)
	    {
	      tree l = build_decl (loc0 (), LABEL_DECL, NULL_TREE,
				   void_type_node);
	      DECL_CONTEXT (l) = cur_fn->decl;
	      DECL_ARTIFICIAL (l) = 1;
	      cur_fn->local_labels.safe_push (get_identifier (peek ().text));
	      cur_fn->local_labels.safe_push (l);
	      next ();
	      if (!accept (","))
		break;
	    }
	  expect (";");
	  continue;
	}
      if (decl_start_p () && !attr_stmt_p ()
	  && !(peek ().kind == TZD_IDENT && is_p (":", 1)))
	parse_declaration (false);
      else
	parse_statement ();
    }
  location_t end = loc0 ();
  expect ("}");
  compound_depth--;

  if (fn_body && MAIN_NAME_P (DECL_NAME (cur_fn->decl))
      && TYPE_MAIN_VARIANT (TREE_TYPE (TREE_TYPE (cur_fn->decl)))
	 == integer_type_node)
    {
      /* main falls off the end: return 0.  */
      tree r = DECL_RESULT (cur_fn->decl);
      add_stmt_tzd (build1_loc (end, RETURN_EXPR, void_type_node,
				build2 (MODIFY_EXPR, integer_type_node, r,
					integer_zero_node)));
    }
  unwind_cleanups (cu_start, end);
  cur_fn->local_labels.truncate (ll_start);

  tree vars = tzd_pop_scope ();
  if (spill && spill->length ())
    {
      /* The slot variables were bound in the parameter scope; declare them
	 here, at the end of the chain (the parameter scope's pop chains
	 them the same way).  */
      tree *tl = &vars;
      while (*tl)
	tl = &DECL_CHAIN (*tl);
      for (unsigned i = 0; i + 1 < spill->length (); i += 2)
	{
	  *tl = (*spill)[i];
	  tl = &DECL_CHAIN ((*spill)[i]);
	}
      *tl = NULL_TREE;
    }
  /* Leaving the block (fall-through, return, goto, break): every automatic
     local -- and, for the function body, every parameter -- is destroyed:
     bytes overwritten twice (zero, then random) and bounds entries removed
     (tzd_sh_wipe).  No data and no stale entry outlives its scope, whatever
     the type (TK 2026-11-07: two-stage destruction on every release and
     scope exit; speed is not a concern here, task #31).  A scalar that
     never has its address taken stays in registers and is not given a
     stack home to wipe (register_only_p; TK 2026-11-14).  */
  {
    tree wipes = NULL_TREE;
    auto_vec<tree> dead;
    for (tree v = vars; v; v = DECL_CHAIN (v))
      if (VAR_P (v) && !TREE_STATIC (v) && !DECL_EXTERNAL (v)
	  /* A register variable is a machine register (rsp!), not
	     memory: nothing to wipe.  */
	  && !DECL_HARD_REGISTER (v))
	dead.safe_push (v);
    if (fn_body)
      for (tree a = DECL_ARGUMENTS (cur_fn->decl); a; a = DECL_CHAIN (a))
	dead.safe_push (a);
    for (tree v : dead)
      if (!register_only_p (v)
	  && TYPE_SIZE_UNIT (TREE_TYPE (v))
	  && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (v))) == INTEGER_CST
	  && !integer_zerop (TYPE_SIZE_UNIT (TREE_TYPE (v))))
	{
	  init_shadow_decls ();
	  TREE_ADDRESSABLE (v) = 1;
	  tree w = build_call_expr_loc (end, tzd_sh_wipe_decl, 2,
					build_fold_addr_expr_loc (end, v),
					fold_convert (size_type_node,
						      TYPE_SIZE_UNIT
							(TREE_TYPE (v))));
	  if (!wipes)
	    wipes = alloc_stmt_list ();
	  append_to_statement_list_force (w, &wipes);
	}
    for (unsigned i = cl_start; i < vec_safe_length (cl_wipes); i++)
      {
	tree v = (*cl_wipes)[i];
	init_shadow_decls ();
	tree w = build_call_expr_loc (end, tzd_sh_wipe_decl, 2,
				      build_fold_addr_expr_loc (end, v),
				      fold_convert (size_type_node,
						    TYPE_SIZE_UNIT
						      (TREE_TYPE (v))));
	if (!wipes)
	  wipes = alloc_stmt_list ();
	append_to_statement_list_force (w, &wipes);
      }
    vec_safe_truncate (cl_wipes, cl_start);
    /* A statement expression's value is its last statement.  The wrapper
       keeps it: the gimplifier's voidify_wrapper_expr walks into the
       TRY_FINALLY_EXPR and stores the value into a temporary inside the
       try body, before the cleanup wipes the locals (task #31).  */
    if (wipes)
      {
	tree tf = build2_loc (end, TRY_FINALLY_EXPR, void_type_node, list,
			      wipes);
	list = alloc_stmt_list ();
	append_to_statement_list_force (tf, &list);
      }
  }
  tree subs = cur_fn->block_chains.pop ();
  tree block = make_node (BLOCK);
  BLOCK_VARS (block) = vars;
  BLOCK_SUBBLOCKS (block) = nreverse (subs);
  for (tree s = BLOCK_SUBBLOCKS (block); s; s = BLOCK_CHAIN (s))
    BLOCK_SUPERCONTEXT (s) = block;
  TREE_USED (block) = 1;
  if (fn_body)
    {
      BLOCK_SUPERCONTEXT (block) = cur_fn->decl;
      DECL_INITIAL (cur_fn->decl) = block;
    }
  else
    {
      tree &top = cur_fn->block_chains.last ();
      BLOCK_CHAIN (block) = top;
      top = block;
    }
  tree bind = build3_loc (loc, BIND_EXPR, void_type_node, vars, list, block);
  TREE_SIDE_EFFECTS (bind) = 1;
  cur_stmts = saved;
  if (fn_body || capture)
    *body = bind;
  else
    add_stmt_tzd (bind);
}

static tree
make_label (location_t loc)
{
  tree l = create_artificial_label (loc);
  DECL_CONTEXT (l) = cur_fn->decl;
  return l;
}

static tree
label_expr (tree l)
{
  return build1 (LABEL_EXPR, void_type_node, l);
}

static tree
goto_expr (tree l)
{
  return build1 (GOTO_EXPR, void_type_node, l);
}

static tree
exit_unless (tree cond, location_t loc)
{
  /* while (1) / do ... while (1): no exit test, as the C front end, so the
     loop is known not to end (-Wreturn-type at -O0).  */
  tree f = fold (cond);
  if (TREE_CODE (f) == INTEGER_CST && !integer_zerop (f))
    return build_empty_stmt (loc);
  return build1_loc (loc, EXIT_EXPR, void_type_node,
		     fold_build2_loc (loc, EQ_EXPR, integer_type_node, cond,
				      integer_zero_node));
}

static tree
paren_cond (void)
{
  location_t loc = loc0 ();
  expect ("(");
  ex c = parse_expr ();
  expect (")");
  return truth (c, loc);
}

static void
parse_loop_body (tree brk, tree cont, tree *list)
{
  cur_fn->break_labels.safe_push (brk);
  cur_fn->continue_labels.safe_push (cont);
  tree *saved = cur_stmts;
  cur_stmts = list;
  sub_stmt_depth++;
  parse_statement ();
  sub_stmt_depth--;
  cur_stmts = saved;
  cur_fn->break_labels.pop ();
  cur_fn->continue_labels.pop ();
}

static void
parse_statement (void)
{
  location_t loc = loc0 ();
  const tzd_token &t = peek ();

  if (is_p ("{"))
    {
      parse_compound (NULL, false);
      return;
    }
  if (attr_stmt_p ())
    {
      /* fallthrough and other statement attributes: hints only.  */
      skip_gnu_noise (false);
      /* `label: __attribute__ ((unused));` (gnulib's _GL_UNUSED_LABEL):
	 the hint is for the label before it, which GCC does not warn about
	 here either.  */
      while (attr_decl_list
	     && is_attribute_p ("unused", TREE_PURPOSE (attr_decl_list)))
	attr_decl_list = TREE_CHAIN (attr_decl_list);
      check_attr_mode_used ();
      expect (";");
      return;
    }
  if (accept (";"))
    return;

  if (t.kind == TZD_IDENT && is_p (":", 1))
    {
      tree l = get_label (get_identifier (t.text), loc);
      next ();
      next ();
      add_stmt_tzd (label_expr (l));
      if (!is_p ("}"))
	parse_statement ();
      return;
    }

  if (t.kind == TZD_KEYWORD)
    {
      if (is_w ("if"))
	{
	  next ();
	  tree c = paren_cond ();
	  tree th = parse_sub_statement ();
	  tree el = NULL_TREE;
	  if (is_w ("else"))
	    {
	      next ();
	      el = parse_sub_statement ();
	    }
	  if (!error_p (c))
	    add_stmt_tzd (build3_loc (loc, COND_EXPR, void_type_node, c, th,
				      el ? el : build_empty_stmt (loc)));
	  return;
	}
      if (is_w ("while"))
	{
	  next ();
	  tree c = paren_cond ();
	  tree brk = make_label (loc), cont = make_label (loc);
	  tree body = alloc_stmt_list ();
	  if (!error_p (c))
	    append_to_statement_list_force (exit_unless (c, loc), &body);
	  parse_loop_body (brk, cont, &body);
	  append_to_statement_list_force (label_expr (cont), &body);
	  add_stmt_tzd (build1_loc (loc, LOOP_EXPR, void_type_node, body));
	  if (TREE_USED (brk))
	    /* Only when a break jumps here: an unused label after an
	       endless loop would make the loop look like it can end
	       (-Wreturn-type, the kernel's for (;;) { ... return ...; }).  */
	    add_stmt_tzd (label_expr (brk));
	  return;
	}
      if (is_w ("do"))
	{
	  next ();
	  tree brk = make_label (loc), cont = make_label (loc);
	  tree body = alloc_stmt_list ();
	  parse_loop_body (brk, cont, &body);
	  if (TREE_USED (cont))
	    append_to_statement_list_force (label_expr (cont), &body);
	  if (!is_w ("while"))
	    error_at (loc0 (), "expected %<while%>");
	  else
	    next ();
	  tree c = paren_cond ();
	  expect (";");
	  if (!error_p (c) && TREE_CODE (c) == INTEGER_CST && integer_zerop (c))
	    /* do ... while (0): the body runs once; no loop and no exit test
	       after it (a body that ends in __builtin_unreachable() must not
	       look like it falls through, -Wreturn-type: the kernel's BUG()).  */
	    add_stmt_tzd (body);
	  else
	    {
	      if (!error_p (c))
		append_to_statement_list_force (exit_unless (c, loc), &body);
	      add_stmt_tzd (build1_loc (loc, LOOP_EXPR, void_type_node, body));
	    }
	  if (TREE_USED (brk))
	    /* Only when a break jumps here: an unused label after an
	       endless loop would make the loop look like it can end
	       (-Wreturn-type, the kernel's for (;;) { ... return ...; }).  */
	    add_stmt_tzd (label_expr (brk));
	  return;
	}
      if (is_w ("for"))
	{
	  next ();
	  expect ("(");
	  /* The for statement is its own scope (C99 declarations).  */
	  tree outer = alloc_stmt_list ();
	  tree *saved = cur_stmts;
	  cur_stmts = &outer;
	  tzd_push_scope ();
	  cur_fn->block_chains.safe_push (NULL_TREE);
	  unsigned cu_start = cleanups.length ();
	  if (decl_start_p ())
	    parse_declaration (false);
	  else
	    {
	      if (!is_p (";"))
		add_stmt_tzd (value_void (rvalue (parse_expr (), loc)));
	      expect (";");
	    }
	  tree c = NULL_TREE;
	  if (!is_p (";"))
	    c = truth (parse_expr (), loc);
	  expect (";");
	  tree incr = NULL_TREE;
	  if (!is_p (")"))
	    incr = value_void (rvalue (parse_expr (), loc));
	  expect (")");
	  tree brk = make_label (loc), cont = make_label (loc);
	  tree body = alloc_stmt_list ();
	  if (c && !error_p (c))
	    append_to_statement_list_force (exit_unless (c, loc), &body);
	  parse_loop_body (brk, cont, &body);
	  append_to_statement_list_force (label_expr (cont), &body);
	  if (incr && !error_p (incr))
	    append_to_statement_list_force (incr, &body);
	  add_stmt_tzd (build1_loc (loc, LOOP_EXPR, void_type_node, body));
	  if (TREE_USED (brk))
	    /* Only when a break jumps here: an unused label after an
	       endless loop would make the loop look like it can end
	       (-Wreturn-type, the kernel's for (;;) { ... return ...; }).  */
	    add_stmt_tzd (label_expr (brk));
	  location_t fend = toks[tpos > 0 ? tpos - 1 : 0].loc;
	  unwind_cleanups (cu_start, fend);
	  tree vars = tzd_pop_scope ();
	  /* The for statement's own variables are destroyed when it is left,
	     like a block's (TK 2026-11-07; they were not wiped before).  */
	  if (tree wipes = scope_wipe_stmts (vars, fend))
	    {
	      tree tf = build2_loc (fend, TRY_FINALLY_EXPR, void_type_node,
				    outer, wipes);
	      outer = alloc_stmt_list ();
	      append_to_statement_list_force (tf, &outer);
	    }
	  tree subs = cur_fn->block_chains.pop ();
	  tree block = make_node (BLOCK);
	  BLOCK_VARS (block) = vars;
	  BLOCK_SUBBLOCKS (block) = nreverse (subs);
	  for (tree s = BLOCK_SUBBLOCKS (block); s; s = BLOCK_CHAIN (s))
	    BLOCK_SUPERCONTEXT (s) = block;
	  tree &top = cur_fn->block_chains.last ();
	  BLOCK_CHAIN (block) = top;
	  top = block;
	  cur_stmts = saved;
	  tree bind = build3_loc (loc, BIND_EXPR, void_type_node, vars, outer,
				  block);
	  TREE_SIDE_EFFECTS (bind) = 1;
	  add_stmt_tzd (bind);
	  return;
	}
      if (is_w ("switch"))
	{
	  next ();
	  expect ("(");
	  ex c = promote (parse_expr (), loc);
	  expect (")");
	  if (!bad (c) && !INTEGRAL_TYPE_P (c.type))
	    {
	      error_at (loc, "switch quantity not an integer");
	      c = err ();
	    }
	  tree brk = make_label (loc);
	  tree type = bad (c) ? integer_type_node : c.type;
	  cur_fn->break_labels.safe_push (brk);
	  cur_fn->switch_types.safe_push (type);
	  tree body = parse_sub_statement ();
	  cur_fn->switch_types.pop ();
	  cur_fn->break_labels.pop ();
	  if (!bad (c))
	    add_stmt_tzd (build2_loc (loc, SWITCH_EXPR, type, c.t, body));
	  if (TREE_USED (brk))
	    /* Only when a break jumps here: an unused label after an
	       endless loop would make the loop look like it can end
	       (-Wreturn-type, the kernel's for (;;) { ... return ...; }).  */
	    add_stmt_tzd (label_expr (brk));
	  return;
	}
      if (is_w ("case") || is_w ("default"))
	{
	  bool dflt = is_w ("default");
	  next ();
	  tree lo = NULL_TREE, hi = NULL_TREE;
	  if (!dflt)
	    {
	      lo = parse_const_int ();
	      if (accept ("..."))
		hi = parse_const_int ();
	    }
	  expect (":");
	  if (cur_fn->switch_types.is_empty ())
	    error_at (loc, "case label not within a switch statement");
	  else
	    {
	      tree type = cur_fn->switch_types.last ();
	      if (lo)
		lo = fold_convert (type, lo);
	      if (hi)
		hi = fold_convert (type, hi);
	      if (dflt || lo)
		add_stmt_tzd (build_case_label (lo, hi, make_label (loc)));
	    }
	  if (!is_p ("}"))
	    parse_statement ();
	  return;
	}
      if (is_w ("break") || is_w ("continue"))
	{
	  bool brk = is_w ("break");
	  next ();
	  expect (";");
	  vec<tree> &v = brk ? cur_fn->break_labels : cur_fn->continue_labels;
	  if (v.is_empty ())
	    error_at (loc, brk ? "break statement not within loop or switch"
		      : "continue statement not within a loop");
	  else
	    {
	      TREE_USED (v.last ()) = 1;
	      add_stmt_tzd (goto_expr (v.last ()));
	    }
	  return;
	}
      if (is_w ("goto"))
	{
	  next ();
	  if (accept ("*"))
	    {
	      /* goto *p (GNU computed goto): to a code address from &&label.
		 Like a call through a function pointer, the target is not
		 checked (code addresses carry no bounds).  */
	      ex e = rvalue (parse_expr (), loc);
	      expect (";");
	      if (bad (e))
		return;
	      tree t;
	      if (TZD_MPTR_P (e.type))
		{
		  parts (e);
		  t = fold_convert (ptr_type_node, e.pa);
		}
	      else if (TREE_CODE (e.type) == POINTER_TYPE)
		t = fold_convert (ptr_type_node, e.t);
	      else
		{
		  error_at (loc, "computed goto needs a pointer");
		  return;
		}
	      add_stmt_tzd (build1_loc (loc, GOTO_EXPR, void_type_node, t));
	      return;
	    }
	  if (peek ().kind != TZD_IDENT)
	    {
	      error_at (loc0 (), "expected label name");
	      skip_to_semi ();
	      return;
	    }
	  tree l = get_label (get_identifier (peek ().text), loc);
	  TREE_USED (l) = 1;
	  next ();
	  expect (";");
	  add_stmt_tzd (goto_expr (l));
	  return;
	}
      if (is_w ("return"))
	{
	  next ();
	  tree res = DECL_RESULT (cur_fn->decl);
	  tree rtype = TREE_TYPE (res);
	  if (is_p (";"))
	    {
	      next ();
	      if (!VOID_TYPE_P (rtype))
		warning_at (loc, 0, "%<return%> with no value in function "
			    "returning non-void");
	      add_stmt_tzd (build1_loc (loc, RETURN_EXPR, void_type_node,
					NULL_TREE));
	      return;
	    }
	  ex e = parse_expr ();
	  expect (";");
	  if (cur_fn->sv_ret)
	    {
	      /* Lowered result (sv): bounds copy into the caller's object.  */
	      e = convert_to (e, cur_fn->lang_ret, loc);
	      if (bad (e))
		return;
	      tree v = value (e);
	      if (!memory_ref_p (v))
		{
		  sorry_at (loc, "tzd: returning a struct that contains "
			    "pointers from a value that is not an object "
			    "(task #46)");
		  return;
		}
	      add_stmt_tzd (aggregate_copy (build_simple_mem_ref
					      (cur_fn->sv_ret), v, loc));
	      add_stmt_tzd (build1_loc (loc, RETURN_EXPR, void_type_node,
					NULL_TREE));
	      return;
	    }
	  if (VOID_TYPE_P (rtype))
	    {
	      if (!bad (e) && !VOID_TYPE_P (e.type))
		error_at (loc, "%<return%> with a value in function returning "
			  "void");
	      add_stmt_tzd (value_void (rvalue (e, loc)));
	      add_stmt_tzd (build1_loc (loc, RETURN_EXPR, void_type_node,
					NULL_TREE));
	      return;
	    }
	  e = convert_to (e, rtype, loc);
	  if (bad (e))
	    return;
	  add_stmt_tzd (build1_loc (loc, RETURN_EXPR, void_type_node,
				    build2 (MODIFY_EXPR, rtype, res,
					    value (e))));
	  return;
	}
    }

  if (is_w ("__asm__") || is_w ("asm") || is_w ("__asm"))
    {
      parse_asm (false);
      return;
    }

  /* Expression statement.  */
  ex e = parse_expr ();
  if (!expect (";"))
    skip_to_semi ();
  if (!bad (e) && compound_depth == stmtexpr_depth && is_p ("}")
      && sub_stmt_depth == stmtexpr_sub_depth)
    {
      /* Last statement of a statement expression: its value.  */
      ex v = rvalue (e, loc);
      if (!bad (v) && sv_type_p (v.type) && memory_ref_p (value (v)))
	{
	  /* A struct holding pointers: copy it with its bounds entries into
	     the result object now, before this block wipes its locals.  */
	  tree res = build_decl (loc, VAR_DECL, NULL_TREE, v.type);
	  DECL_ARTIFICIAL (res) = 1;
	  DECL_CONTEXT (res) = current_function_decl;
	  TREE_ADDRESSABLE (res) = 1;
	  add_stmt_tzd (fold_convert (void_type_node,
				      aggregate_copy (res, value (v), loc)));
	  stmtexpr_type = v.type;
	  stmtexpr_res = res;
	  return;
	}
      if (!bad (v))
	{
	  stmtexpr_type = v.type;
	  add_stmt_tzd (VOID_TYPE_P (v.type) ? value_void (v) : value (v));
	}
      return;
    }
  if (!bad (e))
    add_stmt_tzd (value_void (rvalue (e, loc)));
}

/* ------------------------------------------------------------------ */
/* Translation unit.  */


/* Names an asm template calls ("call NAME", "jmp NAME") in S[0..LEN).  */
static void
asm_template_names (const char *s, size_t len, hash_set<tree> *out)
{
  const char *end = s + len;
  for (const char *c = s; c < end; c++)
    {
      int kw = 0;
      if (end - c > 4 && strncmp (c, "call", 4) == 0)
	kw = 4;
      else if (end - c > 3 && strncmp (c, "jmp", 3) == 0)
	kw = 3;
      if (!kw || (c > s && (ISALNUM (c[-1]) || c[-1] == '_')))
	continue;
      const char *p = c + kw;
      if (p < end && (*p == 'q' || *p == 'l'))
	p++;
      if (p >= end || (*p != ' ' && *p != '\t'))
	continue;
      while (p < end && (*p == ' ' || *p == '\t'))
	p++;
      if (p >= end || !(ISALPHA (*p) || *p == '_'))
	continue;
      const char *q = p;
      while (q < end && (ISALNUM (*q) || *q == '_'))
	q++;
      if (q < end && (*q == '%' || *q == '.'))
	continue;
      out->add (get_identifier_with_length (p, q - p));
      c = q - 1;
    }
}

/* Before parsing: the functions this unit's asm statements call, by an
   "i" operand ([__func] "i" (func), call_on_irqstack) or by name in a
   template ("call __alt_reloc_selftest").  They take the native calling
   convention from their first declaration on, so C calls of them that
   come before the asm (call_on_irqstack_cond's direct branch) agree with
   the asm's.  */
static void
asm_prescan (void)
{
  asm_native_names = new hash_set<tree>;
  for (unsigned k = 0; k < toks.length (); k++)
    {
      const tzd_token &t = toks[k];
      if (t.kind != TZD_STRING)
	continue;
      asm_template_names ((const char *) t.str, t.len, asm_native_names);
      /* "i" (name), "i" (&name), "i" ((name))  */
      if (t.len > 8 || !memchr (t.str, 'i', t.len))
	continue;
      unsigned m = k + 1, open = 0;
      while (m < toks.length () && toks[m].kind == TZD_PUNCT
	     && (strcmp (toks[m].text, "(") == 0
		 || strcmp (toks[m].text, "&") == 0))
	{
	  if (toks[m].text[0] == '(')
	    open++;
	  m++;
	}
      if (open && m < toks.length () && toks[m].kind == TZD_IDENT
	  && m + 1 < toks.length () && toks[m + 1].kind == TZD_PUNCT
	  && strcmp (toks[m + 1].text, ")") == 0)
	asm_native_names->add (get_identifier (toks[m].text));
    }
}

/* C23 attribute specifiers, `[[name]]` and `[[gnu::name (args)]]`, are
   the same hints as `__attribute__ ((name))` (task #71: coreutils 9.11
   writes `[[fallthrough]];`).  `[[` cannot start anything else in C, so
   the token pairs are rewritten here, before any parsing: the matching
   `]]` becomes `))`, a `gnu::` or `__gnu__::` prefix is dropped.  The
   attribute is then accepted or refused by the ordinary rules: an
   attribute that can change layout or behavior is an error, not
   ignored (task #26).  */
static void
rewrite_std_attributes (void)
{
  auto punct = [] (const tzd_token &t, const char *s)
    {
      return t.kind == TZD_PUNCT && strcmp (t.text, s) == 0;
    };
  vec<tzd_token> out = vNULL;
  for (unsigned i = 0; i < toks.length (); i++)
    {
      if (i + 1 < toks.length () && punct (toks[i], "[")
	  && punct (toks[i + 1], "["))
	{
	  unsigned depth = 0, j = i + 2, end = 0;
	  for (; j + 1 < toks.length (); j++)
	    {
	      if (punct (toks[j], "["))
		depth++;
	      else if (punct (toks[j], "]"))
		{
		  if (depth == 0 && punct (toks[j + 1], "]"))
		    {
		      end = j;
		      break;
		    }
		  if (depth)
		    depth--;
		}
	      else if (toks[j].kind == TZD_EOF)
		break;
	    }
	  if (end)
	    {
	      tzd_token a = toks[i], p = toks[i];
	      a.kind = TZD_IDENT;
	      a.text = "__attribute__";
	      p.kind = TZD_PUNCT;
	      p.text = "(";
	      out.safe_push (a);
	      out.safe_push (p);
	      out.safe_push (p);
	      for (unsigned k = i + 2; k < end; k++)
		{
		  if (toks[k].kind == TZD_IDENT
		      && (strcmp (toks[k].text, "gnu") == 0
			  || strcmp (toks[k].text, "__gnu__") == 0)
		      && k + 2 < end && punct (toks[k + 1], ":")
		      && punct (toks[k + 2], ":"))
		    {
		      k += 2;
		      continue;
		    }
		  out.safe_push (toks[k]);
		}
	      p.text = ")";
	      p.loc = toks[end].loc;
	      out.safe_push (p);
	      out.safe_push (p);
	      i = end + 1;
	      continue;
	    }
	}
      out.safe_push (toks[i]);
    }
  toks.release ();
  toks = out;
}

void
tzd_parse_file (void)
{
  toks.truncate (0);
  tpos = 0;
  tzd_token t;
  do
    {
      tzd_lex_next (parse_in, &t);
      if (t.kind == TZD_PUNCT && strcmp (t.text, "::") == 0)
	{
	  /* C has no "::": two colons (asm ("" : "+m" (x) :: "memory")).  */
	  tzd_token c = t;
	  c.text = ":";
	  toks.safe_push (c);
	  toks.safe_push (c);
	  continue;
	}
      toks.safe_push (t);
    }
  while (t.kind != TZD_EOF);

  rewrite_std_attributes ();
  asm_prescan ();

  /* -ftzd-mpx: the MPX instructions of the checks do nothing where MPX
     is missing or off, so the program must not run unchecked: libc2d's
     tzd_mpx_init runs before anything else (.preinit_array; one entry
     per program, a COMDAT group) and stops the program unless MPX is
     there and could be enabled.  */
  if (flag_tzd_mpx)
    {
      if (flag_tzd_kernel)
	error ("%<-ftzd-mpx%> is for user programs, not for the kernel");
      static const char ref[]
	= "\t.pushsection .preinit_array,\"awG\",@preinit_array,__tzd_mpx_ref,comdat\n"
	  "\t.balign 8\n\t.quad tzd_mpx_init\n\t.popsection";
      symtab->finalize_toplevel_asm (build_string (sizeof ref - 1, ref));
    }

  while (peek ().kind != TZD_EOF)
    {
      PROGRESS_GUARD;
      if (accept (";"))
	continue;
      if (is_w ("__asm__") || is_w ("asm") || is_w ("__asm"))
	{
	  parse_asm (true);
	  continue;
	}
      parse_declaration (true);
    }

  tzd_annot_check_unused ();
  if (seen_error ())
    return;
  for (unsigned i = 0; i < vec_safe_length (file_vars); i++)
    {
      tree v = (*file_vars)[i];
      if (!DECL_EXTERNAL (v))
	rest_of_decl_compilation (v, 1, 0);
      if (!DECL_EXTERNAL (v) && DECL_NAME (v)
	  && (TREE_PUBLIC (v) || find_size_decl (DECL_NAME (v)))
	  && !DECL_IN_SYSTEM_HEADER (v)
	  && (TREE_CODE (TREE_TYPE (v)) == ARRAY_TYPE
	      || RECORD_OR_UNION_TYPE_P (TREE_TYPE (v)))
	  && TYPE_SIZE_UNIT (TREE_TYPE (v))
	  && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (v))) == INTEGER_CST)
	{
	  /* Publish the size for units that see only extern T x[].  */
	  tree d = array_size_decl (DECL_NAME (v), true);
	  if (DECL_EXTERNAL (d))
	    {
	      /* Referenced earlier in this unit: now defined here.  */
	      DECL_EXTERNAL (d) = 0;
	      TREE_STATIC (d) = 1;
	    }
	  /* DECL_SIZE_UNIT: with the elements of an initialized flexible
	     array member (fam_init_size).  */
	  DECL_INITIAL (d) = fold_convert (size_type_node,
					   DECL_SIZE_UNIT (v)
					   ? DECL_SIZE_UNIT (v)
					   : TYPE_SIZE_UNIT (TREE_TYPE (v)));
	  TREE_USED (d) = 1;
	  /* A weak definition (the kernel's init/version.c) gives way to
	     a strong one elsewhere, and so does its size.  */
	  if (DECL_WEAK (v))
	    DECL_WEAK (d) = 1;
	  rest_of_decl_compilation (d, 1, 0);
	}
    }
  emit_grp_table ();
  /* The static pointer registrations are emitted after compilation
     (tzd_emit_shinit_late).  */
}

#include "gt-tzd-tzd-parse.h"
