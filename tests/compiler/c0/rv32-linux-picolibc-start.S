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
    /* Linux RV32 ELF entry stack: argc at 0(sp), argv at 4(sp).
     * cJSON's main(void) ignores them; zlib's original example main
     * uses argc/argv and must never see uninitialized argument GPRs. */
    lw a0, 0(sp)
    addi a1, sp, 4
    call main
    /* The real Picolibc exit path flushes buffered stdout and runs
     * registered finalizers before calling our Linux _exit syscall. */
    call exit
1:  j 1b
