/* TzD language hooks.

   cc1tzd = c-family/ (preprocessor, options, builtin types) + the TzD
   front end in this directory (lexer boundary, parser, managed-pointer
   lowering).  GCC's C front end (gcc/c/) is not linked.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "c-family/c-common.h"
#include "c-family/c-objc.h"
#include "tzd-tree.h"

enum c_language_kind c_language = clk_c;

extern size_t tzd_identifier_size (void);
extern size_t c_tree_size (enum tree_code);
extern bool c_missing_noreturn_ok_p (tree);
extern bool c_warn_unused_global_decl (const_tree);
extern void c_initialize_diagnostics (diagnostic_context *);
extern alias_set_type c_get_alias_set (tree);
extern int c_type_dwarf_attribute (const_tree, int);
extern int c_types_compatible_p (tree, tree);
extern tree c_type_promotes_to (tree);
extern tree c_builtin_function (tree);
extern tree c_builtin_function_ext_scope (tree);
extern tree c_simulate_builtin_function_decl (tree);
extern void c_finish_incomplete_decl (tree);
extern void c_incomplete_type_error (location_t, const_tree, const_tree);
extern tree c_reconstruct_complex_type (tree, tree);
extern void c_print_identifier (FILE *, tree, int);
extern const struct scoped_attribute_specs std_attribute_table;

static const scoped_attribute_specs *const tzd_attribute_table[] =
{
  &std_attribute_table,
  &c_common_gnu_attribute_table,
  &c_common_format_attribute_table
};

static const char *
tzd_get_sarif_source_language (const char *)
{
  return "c";
}

static tree
tzd_getdecls (void)
{
  return NULL_TREE;
}

#undef LANG_HOOKS_NAME
#define LANG_HOOKS_NAME "TzD"
#undef LANG_HOOKS_IDENTIFIER_SIZE
#define LANG_HOOKS_IDENTIFIER_SIZE \
  (sizeof (struct c_common_identifier) + 2 * sizeof (void *))
#undef LANG_HOOKS_TREE_SIZE
#define LANG_HOOKS_TREE_SIZE c_tree_size
#undef LANG_HOOKS_INIT
#define LANG_HOOKS_INIT tzd_init
#undef LANG_HOOKS_INIT_TS
#define LANG_HOOKS_INIT_TS c_common_init_ts
#undef LANG_HOOKS_FINISH
#define LANG_HOOKS_FINISH c_common_finish
#undef LANG_HOOKS_OPTION_LANG_MASK
#define LANG_HOOKS_OPTION_LANG_MASK c_common_option_lang_mask
#undef LANG_HOOKS_COMPLAIN_WRONG_LANG_P
#define LANG_HOOKS_COMPLAIN_WRONG_LANG_P c_common_complain_wrong_lang_p
#undef LANG_HOOKS_INIT_OPTIONS_STRUCT
#define LANG_HOOKS_INIT_OPTIONS_STRUCT c_common_init_options_struct
#undef LANG_HOOKS_INIT_OPTIONS
#define LANG_HOOKS_INIT_OPTIONS c_common_init_options
#undef LANG_HOOKS_INITIALIZE_DIAGNOSTICS
#define LANG_HOOKS_INITIALIZE_DIAGNOSTICS c_initialize_diagnostics
#undef LANG_HOOKS_HANDLE_OPTION
#define LANG_HOOKS_HANDLE_OPTION c_common_handle_option
#undef LANG_HOOKS_POST_OPTIONS
#define LANG_HOOKS_POST_OPTIONS c_common_post_options
#undef LANG_HOOKS_GET_ALIAS_SET
#define LANG_HOOKS_GET_ALIAS_SET c_get_alias_set
/* Static pointer registrations: after compilation, so that objects the
   middle end removed are not kept alive by them.  */
#undef LANG_HOOKS_POST_COMPILATION_PARSING_CLEANUPS
#define LANG_HOOKS_POST_COMPILATION_PARSING_CLEANUPS tzd_emit_shinit_late
#undef LANG_HOOKS_PARSE_FILE
#define LANG_HOOKS_PARSE_FILE c_common_parse_file
#undef LANG_HOOKS_FINISH_INCOMPLETE_DECL
#define LANG_HOOKS_FINISH_INCOMPLETE_DECL c_finish_incomplete_decl
#undef LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL
#define LANG_HOOKS_WARN_UNUSED_GLOBAL_DECL c_warn_unused_global_decl
#undef LANG_HOOKS_PRINT_IDENTIFIER
#define LANG_HOOKS_PRINT_IDENTIFIER c_print_identifier
#undef LANG_HOOKS_TYPES_COMPATIBLE_P
#define LANG_HOOKS_TYPES_COMPATIBLE_P c_types_compatible_p
#undef LANG_HOOKS_MISSING_NORETURN_OK_P
#define LANG_HOOKS_MISSING_NORETURN_OK_P c_missing_noreturn_ok_p
#undef LANG_HOOKS_BLOCK_MAY_FALLTHRU
#define LANG_HOOKS_BLOCK_MAY_FALLTHRU c_block_may_fallthru
#undef LANG_HOOKS_BUILTIN_FUNCTION
#define LANG_HOOKS_BUILTIN_FUNCTION c_builtin_function
#undef LANG_HOOKS_BUILTIN_FUNCTION_EXT_SCOPE
#define LANG_HOOKS_BUILTIN_FUNCTION_EXT_SCOPE c_builtin_function_ext_scope
#undef LANG_HOOKS_SIMULATE_BUILTIN_FUNCTION_DECL
#define LANG_HOOKS_SIMULATE_BUILTIN_FUNCTION_DECL c_simulate_builtin_function_decl
#undef LANG_HOOKS_FINALIZE_EARLY_DEBUG
#define LANG_HOOKS_FINALIZE_EARLY_DEBUG c_common_finalize_early_debug
#undef LANG_HOOKS_RECONSTRUCT_COMPLEX_TYPE
#define LANG_HOOKS_RECONSTRUCT_COMPLEX_TYPE c_reconstruct_complex_type
#undef LANG_HOOKS_ATTRIBUTE_TABLE
#define LANG_HOOKS_ATTRIBUTE_TABLE tzd_attribute_table
#undef LANG_HOOKS_TYPE_FOR_MODE
#define LANG_HOOKS_TYPE_FOR_MODE c_common_type_for_mode
#undef LANG_HOOKS_TYPE_FOR_SIZE
#define LANG_HOOKS_TYPE_FOR_SIZE c_common_type_for_size
#undef LANG_HOOKS_INCOMPLETE_TYPE_ERROR
#define LANG_HOOKS_INCOMPLETE_TYPE_ERROR c_incomplete_type_error
#undef LANG_HOOKS_TYPE_PROMOTES_TO
#define LANG_HOOKS_TYPE_PROMOTES_TO c_type_promotes_to
#undef LANG_HOOKS_REGISTER_BUILTIN_TYPE
#define LANG_HOOKS_REGISTER_BUILTIN_TYPE c_register_builtin_type
#undef LANG_HOOKS_TO_TARGET_CHARSET
#define LANG_HOOKS_TO_TARGET_CHARSET c_common_to_target_charset
#undef LANG_HOOKS_GETDECLS
#define LANG_HOOKS_GETDECLS tzd_getdecls
#undef LANG_HOOKS_TYPE_DWARF_ATTRIBUTE
#define LANG_HOOKS_TYPE_DWARF_ATTRIBUTE c_type_dwarf_attribute
#undef LANG_HOOKS_GET_SUBSTRING_LOCATION
#define LANG_HOOKS_GET_SUBSTRING_LOCATION c_get_substring_location
#undef LANG_HOOKS_GET_SARIF_SOURCE_LANGUAGE
#define LANG_HOOKS_GET_SARIF_SOURCE_LANGUAGE tzd_get_sarif_source_language

struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

void
c_family_register_lang_features ()
{
}

#include "gtype-tzd.h"
