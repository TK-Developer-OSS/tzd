/* TzD lexer boundary (see tzd-lex.h).  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "cpplib.h"
#include "input.h"
#include "diagnostic-core.h"
#include "tzd-lex.h"

static struct obstack tzd_lex_obstack;
static bool tzd_lex_obstack_ready;

static const char *
save_text (const unsigned char *s)
{
  return (const char *) obstack_copy0 (&tzd_lex_obstack, s,
				       strlen ((const char *) s));
}

bool tzd_extensions_enabled;

/* Keywords of the C core shared by .c and .d.  */
static const char *const c_keywords[] = {
  "auto", "break", "case", "char", "const", "continue", "default", "do",
  "double", "else", "enum", "extern", "float", "for", "goto", "if",
  "inline", "int", "long", "register", "restrict", "return", "short",
  "signed", "sizeof", "static", "struct", "switch", "typedef", "union",
  "unsigned", "void", "volatile", "while", "_Bool", 0
};

/* Keywords that exist only in TzD (.d) mode.  */
static const char *const tzd_keywords[] = {
  "class", "extends", "abstract", "public", "private", "string",
  "bool", "true", "false", "null", 0
};

static bool
in_list (const char *const *list, const char *s)
{
  for (; *list; list++)
    if (strcmp (*list, s) == 0)
      return true;
  return false;
}

void
tzd_lex_init (const char *main_file)
{
  size_t n = main_file ? strlen (main_file) : 0;
  if (!tzd_lex_obstack_ready)
    {
      gcc_obstack_init (&tzd_lex_obstack);
      tzd_lex_obstack_ready = true;
    }
  tzd_extensions_enabled = n >= 2 && strcmp (main_file + n - 2, ".d") == 0;
}

/* Set by `#pragma tzd <feature>` (declared in tzd-lex.h, registered in
   tzd-opts.cc).  */
bool tzd_pragma_atomics;
bool tzd_pragma_returns_twice;

/* The TYPE.FIELD pairs of `#pragma tzd lazy_pointer` and of
   `#pragma tzd align_member` (with N).  */
#define TZD_LAZY_MAX 64
static const char *tzd_lazy_type[TZD_LAZY_MAX], *tzd_lazy_field[TZD_LAZY_MAX];
static int tzd_lazy_n;
static const char *tzd_al_type[TZD_LAZY_MAX], *tzd_al_field[TZD_LAZY_MAX];
static int tzd_al_n, tzd_al_bytes[TZD_LAZY_MAX];
static const char *tzd_wh_type[TZD_LAZY_MAX], *tzd_wh_field[TZD_LAZY_MAX];
static int tzd_wh_n;
#define TZD_HOLDS_MAX 8
static const char *tzd_fh_type[TZD_LAZY_MAX], *tzd_fh_field[TZD_LAZY_MAX];
static const char *tzd_fh_list[TZD_LAZY_MAX][TZD_HOLDS_MAX + 1];
static int tzd_fh_n;

bool
tzd_fnptr_holds_any (void)
{
  return tzd_fh_n > 0;
}

const char *const *
tzd_fnptr_holds (const char *type, const char *field)
{
  for (int i = 0; i < tzd_fh_n; i++)
    if (strcmp (tzd_fh_type[i], type) == 0
	&& strcmp (tzd_fh_field[i], field) == 0)
      return tzd_fh_list[i];
  return NULL;
}

/* `#pragma tzd managed_override NAME...`.  */
#define TZD_MO_MAX 16
static char *tzd_mo[TZD_MO_MAX];
static int tzd_mo_n;

bool
tzd_managed_override_p (const char *name)
{
  for (int i = 0; i < tzd_mo_n; i++)
    if (strcmp (tzd_mo[i], name) == 0)
      return true;
  return false;
}

static void
managed_override_pragma (cpp_reader *pfile, location_t loc)
{
  int k = 0;
  for (const cpp_token *t = cpp_get_token (pfile);
       t->type != CPP_PRAGMA_EOL && t->type != CPP_EOF;
       t = cpp_get_token (pfile))
    if (t->type != CPP_NAME || tzd_mo_n == TZD_MO_MAX)
      {
	error_at (loc, "%<#pragma tzd managed_override%> takes up to %d "
		  "function names", TZD_MO_MAX);
	k = -1;
      }
    else if (k >= 0)
      {
	tzd_mo[tzd_mo_n++]
	  = xstrdup ((const char *) cpp_token_as_text (pfile, t));
	k++;
      }
  if (k == 0)
    error_at (loc, "%<#pragma tzd managed_override%> takes function names");
}

/* `#pragma tzd refuse_call FUNCTION N`.  */
#define TZD_RC_MAX 16
static char *tzd_rc_name[TZD_RC_MAX];
static long tzd_rc_val[TZD_RC_MAX];
static int tzd_rc_n;

bool
tzd_refuse_call_p (const char *function, long *value)
{
  for (int i = 0; i < tzd_rc_n; i++)
    if (strcmp (tzd_rc_name[i], function) == 0)
      {
	*value = tzd_rc_val[i];
	return true;
      }
  return false;
}

static void
refuse_call_pragma (cpp_reader *pfile, location_t loc)
{
  const cpp_token *a = cpp_get_token (pfile);
  const cpp_token *b = a->type == CPP_NAME ? cpp_get_token (pfile) : a;
  const cpp_token *e = b->type == CPP_NUMBER ? cpp_get_token (pfile) : b;
  if (a->type != CPP_NAME || b->type != CPP_NUMBER
      || e->type != CPP_PRAGMA_EOL || tzd_rc_n == TZD_RC_MAX)
    {
      error_at (loc, "%<#pragma tzd refuse_call%> takes a function name and "
		"an integer");
      for (; e->type != CPP_PRAGMA_EOL && e->type != CPP_EOF;
	   e = cpp_get_token (pfile))
	;
      return;
    }
  tzd_rc_name[tzd_rc_n] = xstrdup ((const char *) cpp_token_as_text (pfile, a));
  tzd_rc_val[tzd_rc_n++]
    = strtol ((const char *) cpp_token_as_text (pfile, b), NULL, 0);
}

/* `#pragma tzd stack_reader FUNCTION...`.  */
#define TZD_SR_MAX 32
static char *tzd_sr[TZD_SR_MAX];
static int tzd_sr_n;

bool
tzd_stack_reader_p (const char *function)
{
  for (int i = 0; i < tzd_sr_n; i++)
    if (strcmp (tzd_sr[i], function) == 0)
      return true;
  return false;
}

static void
stack_reader_pragma (cpp_reader *pfile, location_t loc)
{
  int k = 0;
  for (const cpp_token *t = cpp_get_token (pfile);
       t->type != CPP_PRAGMA_EOL && t->type != CPP_EOF;
       t = cpp_get_token (pfile))
    if (t->type != CPP_NAME || tzd_sr_n == TZD_SR_MAX)
      {
	error_at (loc, "%<#pragma tzd stack_reader%> takes up to %d function "
		  "names", TZD_SR_MAX);
	k = -1;
      }
    else if (k >= 0)
      {
	const char *n = (const char *) cpp_token_as_text (pfile, t);
	if (!tzd_stack_reader_p (n))
	  tzd_sr[tzd_sr_n++] = xstrdup (n);
	k++;
      }
  if (k == 0)
    error_at (loc, "%<#pragma tzd stack_reader%> takes function names");
}

/* The rest of `#pragma tzd fnptr_holds`: NAME . NAME NAME..., the end.  */
static void
fnptr_holds_pragma (cpp_reader *pfile, location_t loc)
{
  const cpp_token *a = cpp_get_token (pfile);
  const cpp_token *dot = a->type == CPP_NAME ? cpp_get_token (pfile) : a;
  const cpp_token *b = dot->type == CPP_DOT ? cpp_get_token (pfile) : dot;
  bool ok = a->type == CPP_NAME && dot->type == CPP_DOT && b->type == CPP_NAME
	    && tzd_fh_n < TZD_LAZY_MAX;
  int k = 0;
  const cpp_token *t = b->type == CPP_NAME ? cpp_get_token (pfile) : b;
  if (ok)
    {
      tzd_fh_type[tzd_fh_n] = xstrdup ((const char *) cpp_token_as_text (pfile, a));
      tzd_fh_field[tzd_fh_n] = xstrdup ((const char *) cpp_token_as_text (pfile, b));
    }
  for (; t->type != CPP_PRAGMA_EOL && t->type != CPP_EOF; t = cpp_get_token (pfile))
    {
      if (t->type != CPP_NAME || k == TZD_HOLDS_MAX)
	ok = false;
      else if (ok)
	tzd_fh_list[tzd_fh_n][k++] = xstrdup ((const char *) cpp_token_as_text (pfile, t));
    }
  if (!ok || k == 0)
    {
      error_at (loc, "%<#pragma tzd fnptr_holds%> takes TYPE.FIELD and 1 to %d "
		"type names", TZD_HOLDS_MAX);
      return;
    }
  tzd_fh_list[tzd_fh_n][k] = NULL;
  tzd_fh_n++;
}

bool
tzd_whole_member_p (const char *type, const char *field)
{
  for (int i = 0; i < tzd_wh_n; i++)
    if (strcmp (tzd_wh_type[i], type) == 0
	&& strcmp (tzd_wh_field[i], field) == 0)
      return true;
  return false;
}

int
tzd_align_member (const char *type, const char *field)
{
  for (int i = 0; i < tzd_al_n; i++)
    if (strcmp (tzd_al_type[i], type) == 0
	&& strcmp (tzd_al_field[i], field) == 0)
      return tzd_al_bytes[i];
  return 0;
}

bool
tzd_lazy_pointer_p (const char *type, const char *field)
{
  for (int i = 0; i < tzd_lazy_n; i++)
    if (strcmp (tzd_lazy_type[i], type) == 0
	&& strcmp (tzd_lazy_field[i], field) == 0)
      return true;
  return false;
}

/* The rest of `#pragma tzd lazy_pointer` / `align_member`: NAME . NAME
   (and N for align_member), then the end.  */
static void
member_pragma (cpp_reader *pfile, location_t loc, bool align, bool whole)
{
  const char *what = align ? "align_member"
		     : whole ? "whole_member" : "lazy_pointer";
  const cpp_token *a = cpp_get_token (pfile);
  const cpp_token *dot = a->type == CPP_NAME ? cpp_get_token (pfile) : a;
  const cpp_token *b = dot->type == CPP_DOT ? cpp_get_token (pfile) : dot;
  const cpp_token *n = b->type == CPP_NAME && align ? cpp_get_token (pfile) : b;
  const cpp_token *e = (align ? n->type == CPP_NUMBER : b->type == CPP_NAME)
		       ? cpp_get_token (pfile) : n;
  int bytes = 0;
  if (align && n->type == CPP_NUMBER)
    bytes = atoi ((const char *) cpp_token_as_text (pfile, n));
  if (a->type != CPP_NAME || dot->type != CPP_DOT || b->type != CPP_NAME
      || e->type != CPP_PRAGMA_EOL
      || (align && (bytes < 1 || bytes > 64 || (bytes & (bytes - 1)))))
    {
      error_at (loc, "%<#pragma tzd %s%> takes TYPE.FIELD%s", what,
		align ? " N (a power of 2, at most 64)" : "");
      while (e->type != CPP_PRAGMA_EOL && e->type != CPP_EOF)
	e = cpp_get_token (pfile);
      return;
    }
  int &cnt = align ? tzd_al_n : whole ? tzd_wh_n : tzd_lazy_n;
  if (cnt == TZD_LAZY_MAX)
    {
      error_at (loc, "too many %<#pragma tzd %s%> directives", what);
      return;
    }
  const char *ta = xstrdup ((const char *) cpp_token_as_text (pfile, a));
  const char *fb = xstrdup ((const char *) cpp_token_as_text (pfile, b));
  if (align)
    {
      tzd_al_type[cnt] = ta;
      tzd_al_field[cnt] = fb;
      tzd_al_bytes[cnt] = bytes;
    }
  else if (whole)
    {
      tzd_wh_type[cnt] = ta;
      tzd_wh_field[cnt] = fb;
    }
  else
    {
      tzd_lazy_type[cnt] = ta;
      tzd_lazy_field[cnt] = fb;
    }
  cnt++;
}

/* ------------------------------------------------------------------ */
/* `// @tzd` annotations (task #79, spec 1).  libcpp calls tzd_lex_comment
   for every comment it lexes -- also in directives and in skipped #if
   blocks -- so an annotation is only taken when the source between its
   end and the next token the parser gets is blank (no directive, no
   other text): it then precedes that token directly.  The source is
   read in libcpp's buffer, only while that buffer is still the current
   one.  */

struct pending_annot {
  tzd_annot *a;
  cpp_buffer *buf;
  const char *file;
  int line;                    /* of the comment's end */
  const unsigned char *end;    /* just after the comment */
};
static vec<pending_annot> pending_annots;
static vec<tzd_annot *> all_annots;

/* Is the line holding TEXT (at column COL of line LINE) part of a
   preprocessor directive: its first non-blank character, or that of a
   line it continues with a backslash, is '#'?  */
static bool
directive_line_p (const unsigned char *text, int line, int col)
{
  const unsigned char *ls = text - (col - 1);
  for (;;)
    {
      for (const unsigned char *q = ls; q < text; q++)
	if (*q != ' ' && *q != '\t' && *q != '\f' && *q != '\v')
	  {
	    if (*q == '#')
	      return true;
	    break;
	  }
      /* Continued from the line before?  Only when that line is not the
	 first (its start is then after a newline we can find).  */
      if (line <= 2 || ls[-1] != '\n')
	return false;
      const unsigned char *pe = ls - 1;   /* the newline */
      if (pe[-1] == '\r')
	pe--;
      if (pe[-1] != '\\')
	return false;
      const unsigned char *q = pe - 1;
      while (q[-1] != '\n')
	q--;
      text = ls = q;
      line--;
      /* The whole previous line is checked from its start.  */
      for (const unsigned char *r = ls; r < pe - 1; r++)
	if (*r != ' ' && *r != '\t' && *r != '\f' && *r != '\v')
	  {
	    if (*r == '#')
	      return true;
	    break;
	  }
    }
}

void
tzd_lex_comment (cpp_reader *pfile, location_t loc, const unsigned char *text,
		 size_t len)
{
  if (len < 2 || text[0] != '/')
    return;
  bool block = text[1] == '*';
  const unsigned char *p = text + 2, *e = text + len;
  if (block && len >= 4)
    e -= 2;
  while (p < e && (*p == ' ' || *p == '\t'))
    p++;
  if (e - p < 4 || memcmp (p, "@tzd", 4) != 0
      || (p + 4 < e && !ISSPACE (p[4])))
    return;
  p += 4;
  expanded_location xl = expand_location (loc);
  if (!xl.file || xl.column <= 0)
    {
      error_at (loc, "%<@tzd%> annotation where its place cannot be told "
		"(task #79)");
      return;
    }
  if (directive_line_p (text, xl.line, xl.column))
    {
      error_at (loc, "%<@tzd%> annotation inside a preprocessor directive "
		"(task #79)");
      return;
    }
  if (!tzd_lex_obstack_ready)
    {
      gcc_obstack_init (&tzd_lex_obstack);
      tzd_lex_obstack_ready = true;
    }
  tzd_annot *a = XOBNEW (&tzd_lex_obstack, tzd_annot);
  memset (a, 0, sizeof *a);
  a->loc = loc;
  for (;;)
    {
      while (p < e && ISSPACE (*p))
	p++;
      if (p >= e)
	break;
      const unsigned char *ks = p;
      while (p < e && (ISALNUM (*p) || *p == '_'))
	p++;
      const unsigned char *ke = p;
      const unsigned char *vs = NULL, *ve = NULL;
      if (p < e && *p == '=')
	{
	  vs = ++p;
	  while (p < e && !ISSPACE (*p))
	    p++;
	  ve = p;
	}
      if (ks == ke || (vs && vs == ve) || (p < e && !ISSPACE (*p)))
	{
	  error_at (loc, "malformed %<@tzd%> annotation: write "
		    "%<key=value%> words (task #79)");
	  a->used = true;
	  return;
	}
      if (a->n == TZD_ANNOT_MAX)
	{
	  error_at (loc, "too many keys in one %<@tzd%> annotation");
	  a->used = true;
	  return;
	}
      a->key[a->n] = (const char *) obstack_copy0 (&tzd_lex_obstack, ks,
						   ke - ks);
      a->val[a->n] = vs ? (const char *) obstack_copy0 (&tzd_lex_obstack, vs,
							ve - vs) : NULL;
      a->n++;
    }
  if (a->n == 0)
    {
      error_at (loc, "empty %<@tzd%> annotation");
      return;
    }
  int end_line = xl.line;
  for (const unsigned char *q = text; q < text + len; q++)
    if (*q == '\n')
      end_line++;
  pending_annot pa = { a, cpp_get_buffer (pfile), xl.file, end_line,
		       text + len };
  pending_annots.safe_push (pa);
  all_annots.safe_push (a);
}

/* Attach the pending annotations to token T (at LOC) just produced.  */
static void
attach_annots (cpp_reader *pfile, const cpp_token *t, location_t loc,
	       struct tzd_token *tok)
{
  location_t xp = linemap_resolve_location (line_table, loc,
					    LRK_MACRO_EXPANSION_POINT, NULL);
  expanded_location tl = expand_location (xp);
  for (unsigned i = 0; i < pending_annots.length (); i++)
    {
      pending_annot &pa = pending_annots[i];
      bool ok = t->type != CPP_EOF && tl.file && tl.column > 0
		&& strcmp (tl.file, pa.file) == 0
		&& cpp_get_buffer (pfile) == pa.buf
		&& tl.line >= pa.line;
      if (ok)
	{
	  /* Blank up to the token: newlines, spaces and tabs only.  */
	  const unsigned char *q = pa.end;
	  int line = pa.line;
	  const unsigned char *ls = NULL;
	  while (ok)
	    {
	      if (line == tl.line)
		{
		  if (!ls)
		    {
		      /* Same line as the comment's end: the token's column
			 is after it.  */
		      ls = q;
		      while (ls[-1] != '\n' && line > 1)
			ls--;
		      if (line == 1)
			{
			  ok = false;
			  break;
			}
		    }
		  if (q >= ls + (tl.column - 1))
		    break;
		}
	      unsigned char c = *q;
	      if (c == '\n')
		{
		  line++;
		  ls = q + 1;
		}
	      else if (c != ' ' && c != '\t' && c != '\r' && c != '\f'
		       && c != '\v')
		ok = false;
	      q++;
	    }
	  ok = ok && q == ls + (tl.column - 1);
	}
      if (!ok)
	{
	  error_at (pa.a->loc, "a %<@tzd%> annotation must come right before "
		    "the declaration it applies to (task #79)");
	  pa.a->used = true;
	  continue;
	}
      if (tok->annot)
	{
	  error_at (pa.a->loc, "two %<@tzd%> annotations for one declaration: "
		    "write the keys in one");
	  pa.a->used = true;
	  continue;
	}
      tok->annot = pa.a;
    }
  pending_annots.truncate (0);
}

void
tzd_annot_check_unused (void)
{
  for (tzd_annot *a : all_annots)
    if (!a->used)
      error_at (a->loc, "this %<@tzd%> annotation applies to nothing "
		"(task #79)");
  all_annots.truncate (0);
}

void
tzd_lex_next (cpp_reader *pfile, struct tzd_token *tok)
{
  const cpp_token *t;
  location_t vloc = UNKNOWN_LOCATION;

  for (;;)
    {
      /* The virtual location: it remembers the macro expansion a token
	 came from, so the parser can tell user code that expands a system
	 header macro from the system header itself (native_region_p).  */
      t = cpp_get_token_with_location (pfile, &vloc);
      if (t->type == CPP_PADDING)
	continue;
      /* A deferred pragma.  `#pragma tzd <feature>` (registered in
	 tzd-opts.cc) sets a TzD directive by its id; any other pragma is
	 skipped to end of line as before (task #26/#61).  */
      if (t->type == CPP_PRAGMA)
	{
	  unsigned id = t->val.pragma;
	  if (id == TZD_PRAGMA_ATOMICS)
	    tzd_pragma_atomics = true;
	  else if (id == TZD_PRAGMA_RETURNS_TWICE)
	    tzd_pragma_returns_twice = true;
	  else if (id == TZD_PRAGMA_FNPTR_HOLDS)
	    {
	      fnptr_holds_pragma (pfile, t->src_loc);
	      continue;
	    }
	  else if (id == TZD_PRAGMA_STACK_READER)
	    {
	      stack_reader_pragma (pfile, t->src_loc);
	      continue;
	    }
	  else if (id == TZD_PRAGMA_REFUSE_CALL)
	    {
	      refuse_call_pragma (pfile, t->src_loc);
	      continue;
	    }
	  else if (id == TZD_PRAGMA_MANAGED_OVERRIDE)
	    {
	      managed_override_pragma (pfile, t->src_loc);
	      continue;
	    }
	  else if (id == TZD_PRAGMA_LAZY_POINTER
		   || id == TZD_PRAGMA_ALIGN_MEMBER
		   || id == TZD_PRAGMA_WHOLE_MEMBER)
	    {
	      member_pragma (pfile, t->src_loc, id == TZD_PRAGMA_ALIGN_MEMBER,
			     id == TZD_PRAGMA_WHOLE_MEMBER);
	      continue;
	    }
	  do
	    t = cpp_get_token (pfile);
	  while (t->type != CPP_PRAGMA_EOL && t->type != CPP_EOF);
	  continue;
	}
      break;
    }

  tok->loc = vloc != UNKNOWN_LOCATION ? vloc : t->src_loc;
  tok->annot = NULL;
  if (pending_annots.length ())
    attach_annots (pfile, t, tok->loc, tok);
  tok->text = "";
  tok->str = NULL;
  tok->len = 0;
  tok->cval = 0;

  switch (t->type)
    {
    case CPP_EOF:
      tok->kind = TZD_EOF;
      return;
    case CPP_NAME:
      tok->text = save_text (cpp_token_as_text (pfile, t));
      tok->kind = (in_list (c_keywords, tok->text)
		   || (tzd_extensions_enabled
		       && in_list (tzd_keywords, tok->text)))
		  ? TZD_KEYWORD : TZD_IDENT;
      return;
    case CPP_NUMBER:
      tok->kind = TZD_NUMBER;
      break;
    case CPP_CHAR: case CPP_WCHAR: case CPP_CHAR16: case CPP_CHAR32:
    case CPP_UTF8CHAR:
      {
	unsigned int chars_seen;
	int unsignedp;
	cppchar_t v = cpp_interpret_charconst (pfile, t, &chars_seen,
					       &unsignedp);
	tok->kind = TZD_CHAR;
	/* Plain char literal: int whose value is the (signed) char.  */
	if (t->type == CPP_CHAR && chars_seen <= 1 && !unsignedp)
	  tok->cval = (signed char) v;
	else
	  tok->cval = (HOST_WIDE_INT) v;
      }
      break;
    case CPP_STRING: case CPP_WSTRING: case CPP_STRING16:
    case CPP_STRING32: case CPP_UTF8STRING:
      {
	cpp_string out;
	tok->kind = TZD_STRING;
	tok->strtype = t->type;
	tok->rawlen = t->val.str.len;
	tok->raw = (const unsigned char *) obstack_copy (&tzd_lex_obstack,
							 t->val.str.text,
							 t->val.str.len);
	if (t->type == CPP_STRING
	    && cpp_interpret_string (pfile, &t->val.str, 1, &out, CPP_STRING))
	  {
	    tok->len = out.len - 1;   /* drop the NUL */
	    tok->str = (const unsigned char *) obstack_copy (&tzd_lex_obstack,
							     out.text,
							     out.len);
	    free (CONST_CAST (unsigned char *, out.text));
	  }
	else
	  tok->str = (const unsigned char *) "";
      }
      break;
    default:
      tok->kind = TZD_PUNCT;
      break;
    }
  tok->text = save_text (cpp_token_as_text (pfile, t));
}

const char *
tzd_tk_name (enum tzd_tk kind)
{
  switch (kind)
    {
    case TZD_EOF: return "EOF";
    case TZD_IDENT: return "IDENT";
    case TZD_KEYWORD: return "KEYWORD";
    case TZD_NUMBER: return "NUMBER";
    case TZD_CHAR: return "CHAR";
    case TZD_STRING: return "STRING";
    case TZD_PUNCT: return "PUNCT";
    }
  return "?";
}

void
tzd_lex_dump (cpp_reader *pfile)
{
  struct tzd_token tok;

  do
    {
      tzd_lex_next (pfile, &tok);
      expanded_location xl = expand_location (tok.loc);
      printf ("%s:%d:%d %-7s %s\n", xl.file ? xl.file : "?", xl.line,
	      xl.column, tzd_tk_name (tok.kind), tok.text);
    }
  while (tok.kind != TZD_EOF);
}
