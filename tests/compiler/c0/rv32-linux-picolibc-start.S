/* Freestanding Linux process entry for the upstream embedded libc.
 * The genuine cJSON implementation and reference use identical CRT and
 * Picolibc. There is no MiniC-specific C runtime substitute. */
    .text
    .globl _start
    .type _start, @function
_start:
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop
    /* Picolibc's hosted CRT invokes constructors and libc initialization
     * before main; custom Linux entry must preserve that ownership. */
    call __libc_init_array
    call main
    li a7, 93
    ecall
1:  j 1b
