.text
.option norelax
.globl _start
.type _start,@function
_start:
    la.local $r12,pcala_low_7ff
    la.local $r12,pcala_low_800
    ld.d $r13,$r12,0
    addi.d $r4,$r13,-123
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start

.data
.p2align 3
    .zero 2047
.globl pcala_low_7ff
pcala_low_7ff:
    .byte 0
.globl pcala_low_800
pcala_low_800:
    .quad 123
