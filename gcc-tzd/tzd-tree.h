/* TzD front end: shared declarations between tzd-decl.cc, tzd-parse.cc
   and tzd-lang.cc.

   cc1tzd does not link GCC's C front end (gcc/c/).  It keeps c-family/
   (preprocessor driver, options, builtin types and functions) and does its
   own parsing, scoping and lowering to GENERIC here.  */
#ifndef TZD_TREE_H
#define TZD_TREE_H

#include "c-family/c-common.h"

/* ---- scopes (tzd-decl.cc) ---- */

/* Bind DECL (with a DECL_NAME) in the current scope; returns DECL.  */
extern tree pushdecl (tree decl);
extern void tzd_push_scope (void);
/* Pop the current scope; returns its decls chained through DECL_CHAIN in
   declaration order (VAR_DECLs and TYPE_DECLs only, for BIND_EXPR_VARS).  */
extern tree tzd_pop_scope (void);
extern bool global_bindings_p (void);
/* Ordinary identifier lookup, innermost scope first.  */
extern tree tzd_lookup (tree id);
/* struct/union/enum tags.  */
extern tree tzd_lookup_tag (tree id, bool current_scope_only);
extern void tzd_push_tag (tree id, tree type);

/* Initialise reserved words, scopes and builtins (LANG_HOOKS_INIT).  */
extern bool tzd_init (void);

/* ---- parser (tzd-parse.cc) ---- */

/* Parse the whole translation unit (replaces c_parse_file).  */
extern void tzd_parse_file (void);

/* ---- managed pointers (tzd-parse.cc) ---- */

/* Managed pointers (task #1, representation (b)).  The language type of a
   data pointer `T *` is a distinct POINTER_TYPE flagged with
   TYPE_LANG_FLAG_1 (the "slot" type): in memory it is C's 8-byte pointer,
   so sizeof, offsetof, struct layout and the host ABI match C.  Its bounds
   live in libc2d's shadow region, keyed by the slot's address.  As a value
   (temporaries, parameters, return values) it is the record
   {T *addr; void *base; size_t size} flagged with TYPE_LANG_FLAG_0.  */
#define TZD_MSLOT_P(TYPE) \
  (TREE_CODE (TYPE) == POINTER_TYPE && TYPE_LANG_FLAG_1 (TYPE))
#define TZD_MREC_P(TYPE) \
  (TREE_CODE (TYPE) == RECORD_TYPE && TYPE_LANG_FLAG_0 (TYPE))
/* Either form.  */
#define TZD_MPTR_P(TYPE) (TZD_MSLOT_P (TYPE) || TZD_MREC_P (TYPE))
/* Pointee type (both forms).  */
#define TZD_MPTR_TARGET(TYPE) \
  (TREE_CODE (TYPE) == POINTER_TYPE ? TREE_TYPE (TYPE) \
   : TYPE_LANG_SLOT_1 (TYPE))
/* Value record of a slot type.  */
#define TZD_MSLOT_REC(TYPE) TYPE_LANG_SLOT_1 (TYPE)

/* Slot (language) type of a managed pointer to POINTEE.  */
extern tree tzd_mptr_type (tree pointee);
extern void tzd_emit_shinit_late (void);
/* Value record type of a managed pointer to POINTEE.  */
extern tree tzd_mrec_type (tree pointee);

#endif
