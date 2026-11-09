/* Tiny genuine RV32 Linux entry: no libc, hidden syscall or MiniC runtime. */
    .text
    .globl _start
    .type _start, @function
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    call main
    li a7, 93
    ecall
1:  j 1b
