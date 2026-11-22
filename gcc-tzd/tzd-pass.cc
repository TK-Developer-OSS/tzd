/* TzD late wipe pass (task #69, TK 2026-11-17).

   When optimizing, the parser leaves the destruction of automatic
   variables (all but volatile ones and variable-length arrays) to this
   pass, which runs after all the GIMPLE optimizations (inlining, scalar
   replacement): by then it is known which of them really live in memory
   and whose address is really taken.  This pass wipes

   - every automatic variable still referenced as memory that has no wipe
     yet (also the compiler's own aggregate temporaries, which the parser
     never saw), and
   - every parameter that is not a register value (an aggregate, such as
     a 24-byte managed pointer, arrives in the caller's argument area).

   Where: at the end of the variable's storage, which the gimplifier
   marked with a clobber at each exit of its scope -- so the variable is
   destroyed when its scope is left, as before, and variables of disjoint
   scopes still share stack bytes (wiping everything at the return made
   them all live there: bash's frames grew past its stack).  A forward
   data-flow pass then finds every variable that can reach a return
   without having been wiped on the way (no clobber: a parameter, an
   inlined parameter's copy, a temporary) and wipes it before that return.

   Then, in user programs, every return of every function is preceded by
   a call of libc2d's tzd_vregs_clear, which clears the whole vector state
   the CPU has (TK 2026-11-17: the safe side, whatever it costs; a lighter
   inline clear of xmm0-15 for functions that cannot run host code was
   tried and withdrawn).  The general registers are zeroed in the epilogue
   by -fzero-call-used-regs=all-gpr.

   The second pass in this file, tzdstack, destroys the stack memory a
   function leaves behind (see pass_tzd_stack).

   A variable that optimization kept in registers gets no stack home and
   no wipe: there is nothing in memory to destroy.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "tree.h"
#include "gimple.h"
#include "tree-pass.h"
#include "ssa.h"
#include "gimple-iterator.h"
#include "gimple-walk.h"
#include "tree-dfa.h"
#include "fold-const.h"
#include "context.h"
#include "cfganal.h"
#include "bitmap.h"
#include "options.h"
#include "stor-layout.h"
#include "tzd-tree.h"
#include "tm.h"
#include "target.h"
#include "rtl.h"
#include "memmodel.h"
#include "emit-rtl.h"
#include "tm_p.h"
#include "regs.h"
#include "stringpool.h"
#include "attribs.h"

extern tree tzd_sh_wipe_fn (void);
extern tree tzd_sh_wipe_data_fn (void);
extern tree tzd_vregs_clear_fn (void);
extern void tzd_register_passes (void);

namespace {

struct collect_data
{
  hash_set<tree> *seen;
  vec<tree> *order;
};

/* walk_tree callback: collect the variables and parameters in *TP.  */
static tree
collect_decls (tree *tp, int *walk_subtrees, void *data)
{
  collect_data *cd = (collect_data *) ((walk_stmt_info *) data)->info;
  tree t = *tp;
  if (VAR_P (t) || TREE_CODE (t) == PARM_DECL)
    {
      if (!cd->seen->add (t))
	cd->order->safe_push (t);
    }
  else if (TYPE_P (t))
    *walk_subtrees = 0;
  return NULL_TREE;
}

/* Whether the bytes of V, an automatic variable or parameter of the
   current function, are destroyed here.  */
static bool
late_wipe_candidate_p (tree v)
{
  if (VAR_P (v))
    {
      if (!auto_var_in_fn_p (v, current_function_decl)
	  || TREE_STATIC (v) || DECL_EXTERNAL (v) || DECL_HARD_REGISTER (v)
	  || DECL_HAS_VALUE_EXPR_P (v))
	return false;
    }
  else if (TREE_CODE (v) != PARM_DECL || DECL_BY_REFERENCE (v))
    return false;
  if (is_gimple_reg (v))
    return false;
  tree sz = TYPE_SIZE_UNIT (TREE_TYPE (v));
  return sz && TREE_CODE (sz) == INTEGER_CST && !integer_zerop (sz);
}

/* Whether memory of TYPE can have bounds-table entries: it holds a
   pointer somewhere (a managed pointer slot, a host struct's raw pointer
   or a function pointer member, which user code may record).  A managed
   pointer value (the record {addr, base, size}) carries its bounds
   itself: no entry.  The variables this pass handles never have their
   address taken, so nothing else (memcpy, a cast) can put an entry on
   them.  */
static bool
may_have_entries_p (tree type)
{
  type = TYPE_MAIN_VARIANT (type);
  if (POINTER_TYPE_P (type))
    return true;
  if (TZD_MREC_P (type))
    return false;
  if (TREE_CODE (type) == ARRAY_TYPE || TREE_CODE (type) == COMPLEX_TYPE
      || TREE_CODE (type) == VECTOR_TYPE)
    return may_have_entries_p (TREE_TYPE (type));
  if (RECORD_OR_UNION_TYPE_P (type))
    {
      for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
	if (TREE_CODE (f) == FIELD_DECL && may_have_entries_p (TREE_TYPE (f)))
	  return true;
      return false;
    }
  return !(INTEGRAL_TYPE_P (type) || SCALAR_FLOAT_TYPE_P (type));
}

const pass_data pass_data_tzd_late_wipe =
{
  GIMPLE_PASS, "tzdwipe", OPTGROUP_NONE, TV_NONE,
  PROP_cfg | PROP_ssa, 0, 0, 0, 0
};

class pass_tzd_late_wipe : public gimple_opt_pass
{
public:
  pass_tzd_late_wipe (gcc::context *ctxt)
    : gimple_opt_pass (pass_data_tzd_late_wipe, ctxt) {}
  unsigned int execute (function *) final override;
};

unsigned int
pass_tzd_late_wipe::execute (function *fun)
{
  tree wipe_fn = tzd_sh_wipe_fn ();
  hash_set<tree> seen, wiped, returned;
  auto_vec<tree> order, rets_dummy;
  auto_vec<gimple *> returns;
  collect_data cd = { &seen, &order };
  bool any_wipe = false;
  basic_block bb;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *s = gsi_stmt (gsi);
	if (is_gimple_debug (s) || gimple_clobber_p (s))
	  continue;
	if (is_gimple_call (s))
	  {
	    tree fd = gimple_call_fndecl (s);
	    if (fd && DECL_NAME (fd) == DECL_NAME (wipe_fn)
		&& gimple_call_num_args (s) == 2)
	      {
		any_wipe = true;
		tree a = gimple_call_arg (s, 0);
		if (TREE_CODE (a) == ADDR_EXPR)
		  if (tree base = get_base_address (TREE_OPERAND (a, 0)))
		    wiped.add (base);
	      }
	  }
	if (greturn *r = dyn_cast <greturn *> (s))
	  {
	    returns.safe_push (s);
	    if (tree rv = gimple_return_retval (r))
	      {
		collect_data rd = { &returned, &rets_dummy };
		walk_stmt_info wi;
		memset (&wi, 0, sizeof wi);
		wi.info = &rd;
		walk_tree (&rv, collect_decls, &wi, NULL);
	      }
	  }
	walk_stmt_info wi;
	memset (&wi, 0, sizeof wi);
	wi.info = &cd;
	walk_gimple_op (s, collect_decls, &wi);
      }
  /* A parameter in memory is destroyed whether the body uses it or not.  */
  for (tree a = DECL_ARGUMENTS (fun->decl); a; a = DECL_CHAIN (a))
    if (!seen.add (a))
      order.safe_push (a);

  auto_vec<tree> todo;
  hash_map<tree, unsigned> idx;
  for (tree v : order)
    if (late_wipe_candidate_p (v) && !wiped.contains (v)
	&& !returned.contains (v))
      {
	idx.put (v, todo.length ());
	todo.safe_push (v);
      }
  (void) any_wipe;
  bool vregs = !flag_tzd_kernel;
  if (todo.is_empty () && !vregs)
    return 0;

  /* Whose address is still taken after the optimizations: something may
     have copied bounds entries into it (memcpy), whatever its type.  */
  hash_set<tree> escaped;
  for (tree v : todo)
    {
      if (TREE_ADDRESSABLE (v))
	escaped.add (v);
      TREE_ADDRESSABLE (v) = 1;
    }

  /* The wipe of V before GSI's statement; returns its last statement.
     One overwrite with a fixed meaningless value, the byte 0xa5 (TK
     2026-11-17: the two stages, zero and then other bytes, added nothing;
     as a word it is not zero, not a small integer and not an address).
     - Memory that may have bounds entries (a pointer in its type, or
       its address still taken: a copy may have brought entries):
       tzd_sh_wipe (bytes overwritten, entries removed).
     - Otherwise up to 64 bytes of whole words: volatile stores of the
       value; nine wipes in ten were a 24-byte pointer parameter (task
       #69).
     - Otherwise tzd_sh_wipe_data (bytes overwritten, no table work).  */
  tree vword = build_qualified_type (long_unsigned_type_node,
				     TYPE_QUAL_VOLATILE);
  tree refall = build_pointer_type_for_mode (char_type_node, ptr_mode, true);
  auto insert_wipe = [&] (gimple_stmt_iterator *gsi, tree v, location_t loc)
    -> gimple *
    {
      unsigned HOST_WIDE_INT sz = tree_to_uhwi (TYPE_SIZE_UNIT (TREE_TYPE (v)));
      gimple *last = NULL;
      bool entries = may_have_entries_p (TREE_TYPE (v))
		     || escaped.contains (v);
      if (!entries && sz <= 64 && sz % 8 == 0 && DECL_ALIGN (v) >= 64)
	{
	  for (unsigned HOST_WIDE_INT off = 0; off < sz; off += 8)
	    {
	      tree lhs = build2 (MEM_REF, vword, build_fold_addr_expr (v),
				 build_int_cst (refall, off));
	      TREE_THIS_VOLATILE (lhs) = 1;
	      TREE_SIDE_EFFECTS (lhs) = 1;
	      tree val = build_int_cstu (long_unsigned_type_node,
					 HOST_WIDE_INT_UC (0xa5a5a5a5a5a5a5a5));
	      last = gimple_build_assign (lhs, val);
	      gimple_set_location (last, loc);
	      gsi_insert_before (gsi, last, GSI_SAME_STMT);
	    }
	  return last;
	}
      tree fn = entries ? wipe_fn : tzd_sh_wipe_data_fn ();
      last = gimple_build_call (fn, 2, build_fold_addr_expr (v),
				build_int_cstu (size_type_node, sz));
      gimple_set_location (last, loc);
      gsi_insert_before (gsi, last, GSI_SAME_STMT);
      return last;
    };
  /* 1. A wipe before each end-of-storage clobber of a whole variable.  */
  hash_map<gimple *, unsigned> ours;
  FOR_EACH_BB_FN (bb, fun)
    for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	 gsi_next (&gsi))
      {
	gimple *s = gsi_stmt (gsi);
	if (!gimple_clobber_p (s, CLOBBER_STORAGE_END))
	  continue;
	tree lhs = gimple_assign_lhs (s);
	unsigned *i = idx.get (lhs);
	if (!i)
	  continue;
	ours.put (insert_wipe (&gsi, lhs, gimple_location (s)), *i);
      }

  /* 2. Which variables can hold data at each point: set by any mention,
     reset by our wipe.  Parameters hold data from the entry.  */
  unsigned n = todo.length ();
  bitmap_obstack obs;
  bitmap_obstack_initialize (&obs);
  int nb = last_basic_block_for_fn (fun);
  auto_vec<bitmap> in (nb), out (nb);
  in.quick_grow_cleared (nb);
  out.quick_grow_cleared (nb);
  FOR_ALL_BB_FN (bb, fun)
    {
      in[bb->index] = BITMAP_ALLOC (&obs);
      out[bb->index] = BITMAP_ALLOC (&obs);
    }
  bitmap entry = out[ENTRY_BLOCK_PTR_FOR_FN (fun)->index];
  for (unsigned i = 0; i < n; i++)
    if (TREE_CODE (todo[i]) == PARM_DECL)
      bitmap_set_bit (entry, i);

  struct mark_data { hash_map<tree, unsigned> *idx; bitmap live; };
  auto mark = [] (tree *tp, int *walk_subtrees, void *data) -> tree
    {
      mark_data *md = (mark_data *) ((walk_stmt_info *) data)->info;
      if (VAR_P (*tp) || TREE_CODE (*tp) == PARM_DECL)
	{
	  if (unsigned *i = md->idx->get (*tp))
	    bitmap_set_bit (md->live, *i);
	}
      else if (TYPE_P (*tp))
	*walk_subtrees = 0;
      return NULL_TREE;
    };
  /* The state after S, given the state LIVE before it.  */
  auto transfer = [&] (gimple *s, bitmap live)
    {
      if (is_gimple_debug (s) || gimple_clobber_p (s))
	return;
      if (unsigned *i = ours.get (s))
	{
	  bitmap_clear_bit (live, *i);
	  return;
	}
      mark_data md = { &idx, live };
      walk_stmt_info wi;
      memset (&wi, 0, sizeof wi);
      wi.info = &md;
      walk_gimple_op (s, mark, &wi);
    };

  int *rpo = XNEWVEC (int, n_basic_blocks_for_fn (fun));
  int nrpo = pre_and_rev_post_order_compute (NULL, rpo, false);
  bitmap tmp = BITMAP_ALLOC (&obs);
  for (bool changed = true; changed;)
    {
      changed = false;
      for (int k = 0; k < nrpo; k++)
	{
	  bb = BASIC_BLOCK_FOR_FN (fun, rpo[k]);
	  edge e;
	  edge_iterator ei;
	  bitmap_clear (in[bb->index]);
	  FOR_EACH_EDGE (e, ei, bb->preds)
	    bitmap_ior_into (in[bb->index], out[e->src->index]);
	  bitmap_copy (tmp, in[bb->index]);
	  for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	       gsi_next (&gsi))
	    transfer (gsi_stmt (gsi), tmp);
	  if (!bitmap_equal_p (tmp, out[bb->index]))
	    {
	      bitmap_copy (out[bb->index], tmp);
	      changed = true;
	    }
	}
    }
  free (rpo);

  /* 3. Before each return: what can still hold data there.  */
  for (gimple *r : returns)
    {
      bb = gimple_bb (r);
      bitmap_copy (tmp, in[bb->index]);
      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); gsi_stmt (gsi) != r;
	   gsi_next (&gsi))
	transfer (gsi_stmt (gsi), tmp);
      gimple_stmt_iterator gsi = gsi_for_stmt (r);
      location_t loc = gimple_location (r);
      unsigned i;
      bitmap_iterator bi;
      EXECUTE_IF_SET_IN_BITMAP (tmp, 0, i, bi)
	insert_wipe (&gsi, todo[i], loc);
      if (vregs)
	{
	  gcall *c = gimple_build_call (tzd_vregs_clear_fn (), 0);
	  gimple_set_location (c, loc);
	  gsi_insert_before (&gsi, c, GSI_SAME_STMT);
	}
    }
  bitmap_obstack_release (&obs);
  return TODO_update_ssa_only_virtuals;
}

/* ------------------------------------------------------------------ */
/* tzdstack: the stack memory a function leaves behind (TK 2026-11-17:
   "the discarded part of the stack must be destroyed, or that is where
   the attack comes from"; relying on push/pop to make data go away is not
   accepted).

   What a function wrote below its return address stays there after it
   returns: locals, the register allocator's spill slots (copies of
   register values no variable wipe knows about), the saved copies of the
   caller's callee-saved registers, outgoing arguments -- and, one word
   lower, the return address its own callees used.  The next function
   that leaves a local uninitialized finds it all.  So, after everything
   else (the pass runs after machine reorg, on the final instructions):

   - before every return, the whole frame below the return address is
     overwritten with the wipe value (the byte 0xa5): N bytes, N from the
     frame layout (plus the red zone, plus the alignment padding of a
     realigned frame).  At that point the epilogue has run; the stack
     pointer is at the return address and the frame is below it, dead.
     An interrupt or signal arriving during the loop uses the same dead
     area and is over before the loop goes on.
   - after every call, the word below the stack pointer (the return
     address the callee just used) is zeroed.

   Not covered: memory taken with alloca below the frame (cc1tzd has no
   alloca; variable-length arrays are wiped by the parser), and the
   frames of code not compiled by cc1tzd (the host libc, libc2d's C
   runtime, the kernel's assembly).  Sibling calls would release a frame
   without a return: they are turned off (tzd-opts.cc).

   The loop uses rcx and rsi: call-clobbered, never part of a return
   value, already zeroed by -fzero-call-used-regs; they are zeroed again
   after it.  A function with another register convention
   (no_caller_saved_registers, interrupt) keeps them: they are saved in
   two words below the area and those words zeroed afterwards.  */

const pass_data pass_data_tzd_stack =
{
  RTL_PASS, "tzdstack", OPTGROUP_NONE, TV_NONE, 0, 0, 0, 0, 0
};

class pass_tzd_stack : public rtl_opt_pass
{
public:
  pass_tzd_stack (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_tzd_stack, ctxt) {}
  unsigned int execute (function *) final override;
};

/* An instruction made of assembler TEXT (no operands).  */
static rtx
tzd_asm_pattern (const char *text, bool regs, bool flags)
{
  rtx body = gen_rtx_ASM_OPERANDS (VOIDmode, ggc_strdup (text), "", 0,
				   rtvec_alloc (0), rtvec_alloc (0),
				   rtvec_alloc (0), UNKNOWN_LOCATION);
  MEM_VOLATILE_P (body) = 1;
  if (!flags)
    return body;
  rtx cc = gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (CCmode, FLAGS_REG));
  if (!regs)
    return gen_rtx_PARALLEL (VOIDmode, gen_rtvec (2, body, cc));
  return gen_rtx_PARALLEL
    (VOIDmode,
     gen_rtvec (4, body,
		gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (DImode, CX_REG)),
		gen_rtx_CLOBBER (VOIDmode, gen_rtx_REG (DImode, SI_REG)), cc));
}

#define TZD_WIPE_WORD "0xa5a5a5a5a5a5a5a5"

/* The wipe of the N bytes below the stack pointer.  KEEP: rcx and rsi
   must come out unchanged.  */
static rtx
tzd_frame_wipe (HOST_WIDE_INT n, bool keep)
{
  char buf[1400], *p = buf;
  if (keep)
    p += sprintf (p, "movq %%%%rcx, -%ld(%%%%rsp)\n\tmovq %%%%rsi, -%ld(%%%%rsp)\n\t",
		  (long) n + 8, (long) n + 16);
  p += sprintf (p, "movabsq $" TZD_WIPE_WORD ", %%%%rsi");
  if (n <= 64)
    for (HOST_WIDE_INT o = 8; o <= n; o += 8)
      p += sprintf (p, "\n\tmovq %%%%rsi, -%ld(%%%%rsp)", (long) o);
  else
    p += sprintf (p, "\n\tleaq -%ld(%%%%rsp), %%%%rcx\n"
		     "7711:\tmovq %%%%rsi, (%%%%rcx)\n\taddq $8, %%%%rcx\n"
		     "\tcmpq %%%%rsp, %%%%rcx\n\tjb 7711b", (long) n);
  if (keep)
    p += sprintf (p, "\n\tmovq -%ld(%%%%rsp), %%%%rcx\n\tmovq -%ld(%%%%rsp), %%%%rsi"
		     "\n\tmovq $0, -%ld(%%%%rsp)\n\tmovq $0, -%ld(%%%%rsp)",
		  (long) n + 8, (long) n + 16, (long) n + 8, (long) n + 16);
  else
    p += sprintf (p, "\n\txorl %%%%ecx, %%%%ecx\n\txorl %%%%esi, %%%%esi");
  return tzd_asm_pattern (buf, !keep, true);
}

unsigned int
pass_tzd_stack::execute (function *fun)
{
  if (lookup_attribute ("naked", DECL_ATTRIBUTES (fun->decl)))
    return 0;
  HOST_WIDE_INT n = fun->machine->frame.stack_pointer_offset - UNITS_PER_WORD
		    + fun->machine->frame.red_zone_size;
  /* A realigned frame starts up to one alignment unit lower than the
     layout says.  */
  if (crtl->stack_realign_needed)
    n += crtl->stack_alignment_needed / BITS_PER_UNIT;
  n = (n + 7) & ~(HOST_WIDE_INT) 7;
  bool keep = fun->machine->call_saved_registers
	      != TYPE_DEFAULT_CALL_SAVED_REGISTERS
	      || fun->machine->func_type != TYPE_NORMAL;
  for (rtx_insn *insn = get_insns (), *next; insn; insn = next)
    {
      next = NEXT_INSN (insn);
      if (CALL_P (insn))
	{
	  if (SIBLING_CALL_P (insn))
	    {
	      /* (Not expected: -foptimize-sibling-calls is off.)  The
		 frame is gone here too; the argument registers stay.  */
	      if (n > 0)
		emit_insn_before (tzd_frame_wipe (n, true), insn);
	    }
	  else if (!find_reg_note (insn, REG_NORETURN, NULL_RTX))
	    emit_insn_after (tzd_asm_pattern ("movq $0, -8(%%rsp)", false,
					      false), insn);
	}
      else if (JUMP_P (insn) && returnjump_p (insn) && n > 0)
	emit_insn_before (tzd_frame_wipe (n, keep), insn);
    }
  return 0;
}

} // anon namespace

void
tzd_register_passes (void)
{
  opt_pass *p = new pass_tzd_late_wipe (g);
  struct register_pass_info info = { p, "nrv", 1, PASS_POS_INSERT_AFTER };
  register_pass (&info);
  opt_pass *q = new pass_tzd_stack (g);
  struct register_pass_info info2 = { q, "mach", 1, PASS_POS_INSERT_AFTER };
  register_pass (&info2);
}
