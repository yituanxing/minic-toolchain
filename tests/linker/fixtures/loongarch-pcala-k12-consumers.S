.text
.globl _start
.type _start,@function
_start:
    b exit_program
    pcalau12i $r12,%pc_hi20(k12_data)
    addi.d $r12,$r12,%pc_lo12(k12_data)
exit_program:
    ori $r4,$r0,0
    ori $r11,$r0,93
    syscall 0
.size _start,.-_start
.data
    .zero 2056
.globl k12_data
k12_data:
    .quad 123
