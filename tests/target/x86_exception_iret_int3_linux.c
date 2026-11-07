/* Linux v6.6.143 genuine source forms from
 *   arch/x86/include/asm/sync_core.h: iret_to_self()
 *   arch/x86/kernel/alternative.c: int3_selftest()
 * with ASM_CALL_CONSTRAINT/ANNOTATE_NOENDBR preprocessor definitions
 * expanded from arch/x86/include/asm/asm.h and objtool annotations.
 * Assemble only: the INT3 requires a kernel notifier to emulate CALL,
 * while IRETQ is an instruction-stream serialization primitive. */
register unsigned long current_stack_pointer asm("rsp");
#define ASM_CALL_CONSTRAINT "+r" (current_stack_pointer)
#define ANNOTATE_NOENDBR "986:\n\t.pushsection .discard.noendbr\n\t.long 986b\n\t.popsection\n\t"

void test_iret_to_self(void) {
    unsigned int tmp;
    asm volatile (
        "mov %%ss, %0\n\t"
        "pushq %q0\n\t"
        "pushq %%rsp\n\t"
        "addq $8, (%%rsp)\n\t"
        "pushfq\n\t"
        "mov %%cs, %0\n\t"
        "pushq %q0\n\t"
        "pushq $1f\n\t"
        "iretq\n\t"
        "1:"
        : "=&r" (tmp), ASM_CALL_CONSTRAINT : : "cc", "memory");
}

void test_int3_selftest(unsigned int *val) {
    asm volatile ("int3_selftest_ip:\n\t"
                  ANNOTATE_NOENDBR
                  "    int3; nop; nop; nop; nop\n\t"
                  : ASM_CALL_CONSTRAINT
                  : "D" (val)
                  : "memory");
}
