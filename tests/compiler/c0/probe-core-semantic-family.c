#include "target/common/core_instruction_family.h"
#include <stdio.h>
/* Semantic classification is NOT an ISA/ABI permission bitset. */
int main(void) {
    unsigned int i;
    for (i = 0U; i < (unsigned int)MINIC_CORE_INSTRUCTION_KIND_COUNT; ++i) {
        const MinicCoreSemanticFamily family =
            minic_core_instruction_semantic_family((MinicCoreInstructionKind)i);
        /* Four numeric IDs between opaque and structured ASM were retired.
         * Preserving their holes is intentional ABI stability, not a new
         * live Core opcode that the target vocabulary failed to classify. */
        if (i > (unsigned int)MINIC_CORE_INSTRUCTION_OPAQUE_INLINE_ASM &&
            i < (unsigned int)MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM) {
            if (family != MINIC_CORE_FAMILY_INVALID) return 1;
        } else if (family == MINIC_CORE_FAMILY_INVALID) return 1;
    }
    if (minic_core_instruction_semantic_family((MinicCoreInstructionKind)-1) !=
            MINIC_CORE_FAMILY_INVALID ||
        minic_core_instruction_semantic_family(
            (MinicCoreInstructionKind)MINIC_CORE_INSTRUCTION_KIND_COUNT) !=
            MINIC_CORE_FAMILY_INVALID ||
        minic_core_instruction_semantic_family(MINIC_CORE_INSTRUCTION_ATOMIC_STORE) !=
            MINIC_CORE_FAMILY_MEMORY ||
        minic_core_instruction_semantic_family(MINIC_CORE_INSTRUCTION_VARIADIC_COPY) !=
            MINIC_CORE_FAMILY_ABI ||
        minic_core_instruction_semantic_family(MINIC_CORE_INSTRUCTION_FIXED_REGISTER_WRITE) !=
            MINIC_CORE_FAMILY_ABI ||
        minic_core_instruction_semantic_family(
            MINIC_CORE_INSTRUCTION_VECTOR_STORAGE_COPY) !=
            MINIC_CORE_FAMILY_MEMORY ||
        minic_core_instruction_semantic_family(
            MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_LOAD) !=
            MINIC_CORE_FAMILY_MEMORY ||
        minic_core_instruction_semantic_family(
            MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_STORE) !=
            MINIC_CORE_FAMILY_MEMORY ||
        minic_core_instruction_semantic_family(
            MINIC_CORE_INSTRUCTION_FIXED_VECTOR128_XOR) !=
            MINIC_CORE_FAMILY_MEMORY ||
        !minic_core_instruction_is_call(MINIC_CORE_INSTRUCTION_INDIRECT_CALL) ||
        !minic_core_instruction_is_integer_scalar(MINIC_CORE_INSTRUCTION_INTEGER_CLZ) ||
        !minic_core_instruction_is_floating_scalar(MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE) ||
        !minic_core_instruction_is_memory_access(MINIC_CORE_INSTRUCTION_ATOMIC_LOAD) ||
        !minic_core_instruction_is_inline_asm(
            MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM)) return 2;
    printf("MINIC_FIVE_TARGET_CORE_FAMILY=PASS opcodes=%u\n",
           (unsigned)MINIC_CORE_INSTRUCTION_KIND_COUNT);
    return 0;
}
