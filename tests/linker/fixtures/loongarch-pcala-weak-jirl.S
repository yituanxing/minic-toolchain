.text
.globl _start
.type _start,@function
_start:
    b exit_program
    la.local $r12,optional_jirl_weak
exit_program:
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start

.weak optional_jirl_weak
.type optional_jirl_weak,@function
