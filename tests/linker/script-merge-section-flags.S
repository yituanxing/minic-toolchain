.section .text,"ax",@progbits
.globl _start
_start:
    lea merge_text(%rip), %rsi
    mov $1, %edi
    mov $1, %eax
    mov $6, %edx
    syscall
    mov $60, %eax
    xor %edi, %edi
    syscall

.section .rodata.str1.1,"aMS",@progbits,1
.globl merge_text
merge_text:
    .asciz "alpha\n"

.section .rodata.cst4,"aM",@progbits,4
.p2align 2
.globl merge_const
merge_const:
    .long 0x12345678

.section .rodata,"a",@progbits
.globl normal_const
normal_const:
    .quad 0x0123456789abcdef

.section .note.GNU-stack,"",@progbits
