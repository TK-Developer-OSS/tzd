/* TzD lexer boundary.

   Everything after the lexer (parser, type resolution, codegen) sees only
   tzd_token.  The preprocessor (libcpp) is behind this wall, so it can be
   replaced or extended (new keywords, new punctuators, new literal forms)
   without touching the later stages.  */
#ifndef TZD_LEX_H
#define TZD_LEX_H

enum tzd_tk {
  TZD_EOF,
  TZD_IDENT,
  TZD_KEYWORD,
  TZD_NUMBER,    /* integer or floating literal, spelled in text */
  TZD_CHAR,      /* character literal */
  TZD_STRING,    /* string literal */
  TZD_PUNCT      /* operators and punctuation, spelled in text */
};

struct tzd_token {
  enum tzd_tk kind;
  location_t loc;
  const char *text;          /* spelling; stays valid for the whole TU */
  /* TZD_STRING: interpreted bytes without the terminating NUL (narrow
     strings; empty for the others).  */
  const unsigned char *str;
  size_t len;
  /* TZD_STRING: the literal as spelled (prefix and quotes included) and
     its kind (CPP_STRING, CPP_WSTRING, ...), for parse_string to
     interpret a sequence of literals together (wide ones).  */
  const unsigned char *raw;
  unsigned int rawlen;
  int strtype;
  /* TZD_CHAR: value as int (C rules for plain char literals).  */
  HOST_WIDE_INT cval;
  /* A `// @tzd ...` annotation right before this token (task #79), or
     NULL.  */
  struct tzd_annot *annot;
};

/* An annotation comment, `// @tzd key=value ...` or the block form
   (task #79, spec 1): ordinary compilers see a comment.  It applies to
   the declaration that starts on the next line (only blank lines between
   them); the parser takes it from that declaration's first token and sets
   USED.  One that applies to nothing, and an unknown key, are errors.  */
#define TZD_ANNOT_MAX 8
struct tzd_annot {
  location_t loc;
  unsigned n;
  const char *key[TZD_ANNOT_MAX];
  const char *val[TZD_ANNOT_MAX];   /* NULL: a key without "=value" */
  bool used;
};
/* libcpp's comment callback (set in tzd-opts.cc).  */
extern void tzd_lex_comment (cpp_reader *, location_t, const unsigned char *,
			     size_t);
/* Errors for the annotations no declaration took (end of the unit).  */
extern void tzd_annot_check_unused (void);

/* Is the current translation unit TzD (.d) rather than Managed C (.c)?  */
extern bool tzd_extensions_enabled;

/* `#pragma tzd <feature>` directives (task #26/#61): each enables one
   explicit, auditable relaxation for the files that opt in (mostly
   OpenSSL); the default stays strict.  Registered as deferred pragmas in
   tzd-opts.cc with these ids, set in tzd-lex.cc, read where they apply.  */
#define TZD_PRAGMA_ATOMICS        1
#define TZD_PRAGMA_RETURNS_TWICE  2
#define TZD_PRAGMA_LAZY_POINTER   3
#define TZD_PRAGMA_ALIGN_MEMBER   4
#define TZD_PRAGMA_WHOLE_MEMBER   5
#define TZD_PRAGMA_FNPTR_HOLDS    6
extern bool tzd_pragma_atomics;         /* accept C11 _Atomic (scalars) */
extern bool tzd_pragma_returns_twice;   /* accept the returns_twice attribute */
/* `#pragma tzd lazy_pointer TYPE.FIELD` (task #70): the pointer member
   FIELD of union TYPE (tag or typedef name) may hold another member's
   bytes when read; such a read gives a pointer with no bounds (any access
   through it traps) instead of trapping at the read.  Other loads stay
   strict.  */
extern bool tzd_lazy_pointer_p (const char *type, const char *field);
/* `#pragma tzd align_member TYPE.FIELD N` (task #70): member FIELD of
   struct TYPE (tag, or the typedef name right after an untagged body) is
   aligned to N bytes, as with __attribute__((aligned (N))) on it -- for a
   byte buffer that receives copies of memory holding pointers (bash's
   SAVED_VAR.desired_setting), so the copied slots stay 8-byte aligned and
   keep their bounds.  Returns N, or 0.  */
extern int tzd_align_member (const char *type, const char *field);
/* `#pragma tzd whole_member TYPE.FIELD` (task #70): &p->FIELD of struct or
   union TYPE keeps the bounds p was reached with (the whole allocation)
   instead of narrowing to the member -- bison's stack relocation copies a
   whole stack to &yyptr->yyss_alloc of union yyalloc.  */
extern bool tzd_whole_member_p (const char *type, const char *field);
/* `#pragma tzd fnptr_holds TYPE.FIELD T1 [T2 ...]` (task #70): the
   function-pointer member FIELD of struct TYPE may also hold pointers to
   T1, T2, ... (struct tags, typedef names or `char`), cast to the function
   pointer type (readline's KEYMAP_ENTRY.function: a function, a child
   keymap or a macro string).  Such a store records the data pointer's
   bounds and its type; reading the member back as a T * checks the type.
   Returns the list (NULL-terminated) or NULL.  */
extern const char *const *tzd_fnptr_holds (const char *type, const char *field);
extern bool tzd_fnptr_holds_any (void);
/* `#pragma tzd stack_reader FUNCTION...` (task #72, kernel): in the named
   functions -- the stack unwinder's -- reading a word through a pointer
   that is out of its bounds is allowed when the word lies in a kernel
   stack of the running task: the runtime (kc2d's tzd_stack_view) hands
   back the address of a private copy, so nothing can be written to the
   stack this way.  */
#define TZD_PRAGMA_STACK_READER 7

extern bool tzd_stack_reader_p (const char *function);
/* `#pragma tzd refuse_call FUNCTION N` (task #71): a call of the named
   function does not happen; its arguments are evaluated and the call
   yields the int N.  For services the project's runtime cannot give
   safely: pthread_create (no thread-safe bounds table; a start routine
   called by the host gets pointers of unknown length) refused with
   EAGAIN (11) is "no more threads", which callers handle.  */
#define TZD_PRAGMA_REFUSE_CALL 8
extern bool tzd_refuse_call_p (const char *function, long *value);

/* `#pragma tzd managed_override NAME...` (task #71): libc functions the
   program defines itself in Managed C (gnulib's error and error_at_line,
   which replace glibc's): every declaration of them, also the system
   header's, takes the managed calling convention, so callers and the
   definition agree.  */
#define TZD_PRAGMA_MANAGED_OVERRIDE 9
extern bool tzd_managed_override_p (const char *name);

extern void tzd_lex_init (const char *main_file);
extern void tzd_lex_next (cpp_reader *pfile, struct tzd_token *tok);
extern const char *tzd_tk_name (enum tzd_tk kind);

/* Debug aid for -ftzd-dump-tokens.  */
extern void tzd_lex_dump (cpp_reader *pfile);

#endif
