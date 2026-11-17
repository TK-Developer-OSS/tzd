/* TzD directives for building the Linux kernel under TzD, force-included
   in every file cc1tzd compiles (tests/kernel/tzd-cc.sh), so the
   exceptions the kernel needs are stated in one auditable place; the
   default elsewhere stays strict.  No kernel source is changed for them.

   - stack_reader (task #72, TK 2026-11-17: by annotation, not by
     changing the kernel): the stack unwinder follows the frame pointer
     chain and reads the stack word by word.  The chain starts at
     __builtin_frame_address, which TzD gives no readable bounds, so the
     first read trapped -- and a WARN, an OOM report or a panic, which all
     print a stack trace, ended in "Fatal exception".  In these three
     functions a word may be read out of its pointer's bounds when it
     lies in a kernel stack of the running task; the read goes through a
     private copy (kc2d's tzd_stack_view), so nothing can be written to
     the stack that way.
       __read_once_word_nocheck  READ_ONCE_NOCHECK (include/asm-generic/rwonce.h):
                                 show_trace_log_lvl, unwind_dump
       update_stack_state        READ_ONCE_TASK_STACK of the return address
       unwind_next_frame         READ_ONCE_TASK_STACK of the next frame pointer
     (arch/x86/kernel/unwind_frame.c)  */
#pragma tzd stack_reader __read_once_word_nocheck update_stack_state unwind_next_frame
