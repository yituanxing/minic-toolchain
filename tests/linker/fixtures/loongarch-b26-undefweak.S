.text
.globl _start
.type _start,@function
_start:
    b exit_program
    b optional_branch_target
    bl optional_branch_target
exit_program:
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start

.weak optional_branch_target
.type optional_branch_target,@function
