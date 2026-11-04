.section .text,"ax",@progbits
.globl _start
_start:
    movzbl pi_code(%rip), %eax
    cmpl $81, %eax
    jne bad_pi
    movq pi_bss(%rip), %rax
    testq %rax, %rax
    jne bad_pi
    movq pi_bss+8(%rip), %rax
    testq %rax, %rax
    jne bad_pi
    movq pi_tail(%rip), %rax
    cmpq $37, %rax
    jne bad_pi
    xorl %edi, %edi
    movl $60, %eax
    syscall
bad_pi:
    movl $1, %edi
    movl $60, %eax
    syscall

.section .init.pi.code,"ax",@progbits
.globl pi_code
pi_code:
    .byte 81, 0, 0, 0, 0, 0, 0, 0

.section .init.pi.bss,"aw",@nobits
.balign 8
.globl pi_bss
pi_bss:
    .zero 16

.section .init.pi.tail,"aw",@progbits
.balign 8
.globl pi_tail
pi_tail:
    .quad 37
