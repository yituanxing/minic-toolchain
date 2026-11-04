.section .text,"ax",@progbits
.globl _start
_start:
    addi a0, zero, 0
    addi a7, zero, 93
    ecall
.section .rodata,"a",@progbits
    .long 0x12345678
.section .rodata.mutable,"awT",@progbits
    .long 0xdeadbeef
.section .rodata.tls,"awT",@progbits
    .long 0xdecafbad
.section .rodata.nobits,"a",@nobits
    .space 4
