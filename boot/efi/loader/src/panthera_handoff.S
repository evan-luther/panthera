.text

.globl panthera_handoff_template_begin
.globl panthera_handoff_template_end
.globl panthera_handoff_gdtr
.globl panthera_handoff_gdt

.set PANTHERA_CODE32_SEL, 0x08
.set PANTHERA_DATA32_SEL, 0x10
.set CR0_PG, 0x80000000

.code64
panthera_handoff_template_begin:
  cli
  movl %ecx, %ebx
  movl %edx, %esi
  movl %r8d, %edi
  lea panthera_handoff_gdtr(%rip), %rax
  lgdt (%rax)
  movb $'A', %al
  outb %al, $0xe9
  movq %r8, %rsp
  pushq $PANTHERA_CODE32_SEL
  lea panthera_handoff_compat(%rip), %rax
  pushq %rax
  lretq

.code32
panthera_handoff_compat:
  movb $'B', %al
  outb %al, $0xe9
  movw $PANTHERA_DATA32_SEL, %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %fs
  movw %ax, %gs
  movw %ax, %ss
  movl %edi, %esp
  movl %cr0, %eax
  andl $(~CR0_PG), %eax
  movl %eax, %cr0
  jmp panthera_handoff_protected

panthera_handoff_protected:
  movb $'C', %al
  outb %al, $0xe9
  movw $PANTHERA_DATA32_SEL, %ax
  movw %ax, %ds
  movw %ax, %es
  movw %ax, %fs
  movw %ax, %gs
  movw %ax, %ss
  movl %edi, %esp
  movb $'J', %al
  outb %al, $0xe9
  movl %ebx, %eax
  jmp *%esi

  ud2

  .align 8
panthera_handoff_gdtr:
  .word panthera_handoff_gdt_end - panthera_handoff_gdt - 1
  .quad panthera_handoff_gdt

  .align 8
panthera_handoff_gdt:
  .quad 0x0000000000000000
  .quad 0x00cf9a000000ffff
  .quad 0x00cf92000000ffff
panthera_handoff_gdt_end:

panthera_handoff_template_end:
