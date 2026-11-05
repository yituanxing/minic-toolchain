.section .text,"ax",@progbits
.globl _start
_start:
    addi a0, zero, 0
    addi a7, zero, 93
    ecall

.section .rodata,"a",@progbits
.globl ro_const
ro_const:
    .quad 0x0123456789abcdef

.section .data..ro_after_init,"aw",@progbits
.globl late_ro_const
late_ro_const:
    .quad 0xabcdef0123456789
