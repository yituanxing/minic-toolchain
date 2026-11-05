.section .text,"ax",@progbits
.globl _start
_start:
    addi a0, zero, 0
    addi a7, zero, 93
    ecall
.section .rodata.str1.1,"aMS",@progbits,1
.globl first_text
first_text:
    .asciz "first"
.section .rodata.str1.2,"aMS",@progbits,1
.globl second_text
second_text:
    .asciz "second"
