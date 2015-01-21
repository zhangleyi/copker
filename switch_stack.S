
.LC0:
	.string	"esp+8 para:%lx esp+12 function:%lx esp+16 stackBottom:%lx\n"
.LC1:
	.string	"private_key_compute success\n"
.LC2:
	.string	"ebp:%lx esp:%lx\n"
.LC3:
	.string	"edx:%lx\n"
.LC4:
	.string	"&env:%lx\n"
			
	.text
	.globl	switch_stack
	.type	switch_stack, @function
switch_stack:
.LFB0:

	pushl	%ebp
	movl	%esp, %ebp

	subl	$24, %esp
	
	movl 16(%ebp),%eax
	movl %ebp,(%eax)
	movl %esp,-4(%eax)
	
	movl %ebp,%ebx
	
	movl %eax,%ebp
	movl %eax,%esp
	subl $4,%esp
	pushl 8(%ebx)
	
	call 12(%ebx)
	
	movl %ebp,%ebx
	movl (%ebx),%ebp
	movl -4(%ebx),%esp
			
	leave
	ret


