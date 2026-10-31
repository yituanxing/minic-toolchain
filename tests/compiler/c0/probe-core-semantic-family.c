#include "target/common/core_instruction_family.h"
#include <stdio.h>
/* Semantic classification is NOT an ISA/ABI permission bitset. */
int main(void) {
    unsigned int i;
    for (i = 0U; i <= (unsigned int)MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE; ++i)
        if (minic_core_instruction_semantic_family((MinicCoreInstructionKind)i) ==
            MINIC_CORE_FAMILY_INVALID) return 1;
    if (minic_core_instruction_semantic_family((MinicCoreInstructionKind)-1) !=
            MINIC_CORE_FAMILY_INVALID ||
        minic_core_instruction_semantic_family(
            (MinicCoreInstructionKind)(MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE + 1)) !=
            MINIC_CORE_FAMILY_INVALID ||
        minic_core_instruction_semantic_family(MINIC_CORE_INSTRUCTION_ATOMIC_STORE) !=
            MINIC_CORE_FAMILY_MEMORY ||
        minic_core_instruction_semantic_family(MINIC_CORE_INSTRUCTION_VARIADIC_COPY) !=
            MINIC_CORE_FAMILY_ABI ||
        minic_core_instruction_semantic_family(MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE) !=
            MINIC_CORE_FAMILY_ABI ||
        !minic_core_instruction_is_call(MINIC_CORE_INSTRUCTION_INDIRECT_CALL) ||
        !minic_core_instruction_is_integer_scalar(MINIC_CORE_INSTRUCTION_INTEGER_CLZ) ||
        !minic_core_instruction_is_floating_scalar(MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE) ||
        !minic_core_instruction_is_memory_access(MINIC_CORE_INSTRUCTION_ATOMIC_LOAD) ||
        !minic_core_instruction_is_inline_asm(
            MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)) return 2;
    printf("MINIC_FIVE_TARGET_CORE_FAMILY=PASS opcodes=%u\n",
           (unsigned)MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE + 1U);
    return 0;
}
