/* libc2d process entry (x86-64 Linux).  Replaces glibc's crt1.o.  */
	.text
	.globl	_start
	.type	_start, @function
_start:
	xorl	%ebp, %ebp
	movq	%rsp, %rdi		/* initial stack: argc, argv[], 0, envp[] */
	andq	$-16, %rsp
	call	tzd_start_main
	hlt
	.size	_start, .-_start
	.section .note.GNU-stack,"",@progbits
