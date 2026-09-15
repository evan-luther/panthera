.text
.globl _panthera_fork_child_aware
.p2align 4, 0x90
_panthera_fork_child_aware:
	movl	$0x2000002, %eax
	syscall
	jc	.Lfork_error
	testl	%edx, %edx
	jz	.Lfork_parent
	xorl	%eax, %eax
.Lfork_parent:
	ret
.Lfork_error:
	negq	%rax
	ret
