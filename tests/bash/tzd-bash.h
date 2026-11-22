/* TzD directives for building bash 5.3 under TzD (task #70), force-included
   in every file of the TzD bash build (tests/bash/build_tzd.sh), so the
   exceptions bash needs are stated in one auditable place; the default
   elsewhere stays strict.

   - lazy_pointer REDIRECTEE.filename (TK 2026-11-16): REDIRECTEE is
     union { int dest; WORD_DESC *filename; }.  bash reads .filename ahead
     of knowing which member is live (redir.c do_redirection_internal,
     print_cmd.c print_redirection) and uses it only when it is the
     filename.  Such a read gives a pointer with no bounds instead of
     trapping at the read; any access through it still traps.  */
#pragma tzd lazy_pointer REDIRECTEE.filename

/* - align_member SAVED_VAR.desired_setting 8 (task #70): unwind-protect
     (unwind_prot.c) saves a variable's bytes there and copies them back on
     unwind.  The member sits at offset 28 of UNWIND_ELT; the bounds table
     keys 8-byte aligned slots, so a saved pointer lost its bounds and the
     restored variable trapped when read (subst_assign_varlist).  Aligned
     to 8, the copies keep their bounds.  */
#pragma tzd align_member SAVED_VAR.desired_setting 8

/* - whole_member yyalloc.yyss_alloc (task #70): bison's YYSTACK_RELOCATE
     (y.tab.c) copies the whole state stack to &yyptr->yyss_alloc of the
     newly allocated array of union yyalloc and keeps indexing from there.
     The member's address keeps the allocation's bounds instead of one
     element's (yyvs_alloc, a union at offset 0, already does).  */
#pragma tzd whole_member yyalloc.yyss_alloc

/* - fnptr_holds _keymap_entry.function _keymap_entry char (TK 2026-11-16):
     readline's KEYMAP_ENTRY.function holds a command function (ISFUNC), a
     child keymap (ISKMAP: KEYMAP_TO_FUNCTION / FUNCTION_TO_KEYMAP) or a
     macro string (ISMACR), cast to rl_command_func_t *.  A data pointer
     stored there is recorded with its bounds and its type; reading it back
     as a Keymap or a char * checks the type (a char view, for xfree, takes
     either).  The static keymap tables are registered at startup.  */
#pragma tzd fnptr_holds _keymap_entry.function _keymap_entry char
