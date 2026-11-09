    .text
    .globl _start
_start:
    call mini_stack_forward_test_main
    li a7, 93
    ecall
1:  j 1b
