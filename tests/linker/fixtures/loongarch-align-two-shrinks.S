.text
.option relax
.globl _start
.type _start,@function
_start:
    b aligned_two
    ori $r4,$r0,0
    .p2align 4
aligned_one:
    nop
    nop
    .p2align 4
aligned_two:
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start
