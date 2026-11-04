.text
.globl _start
.type _start,@function
_start:
    la.local $r12,optional_weak
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start

.weak optional_weak
.type optional_weak,@function
