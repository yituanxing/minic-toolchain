#include "target/common/core_instruction_family.h"
#include <stdio.h>

/* Exhaustive ABI-neutral membership guard for every current Core kind.
 * Compiler-generated M/A/F instructions are never inferred from membership. */
int main(void) {
    unsigned int i, integer = 0U, floating = 0U, calls = 0U;
    for (i = 0U; i < (unsigned int)MINIC_CORE_INSTRUCTION_KIND_COUNT; ++i) {
        MinicCoreInstructionKind kind = (MinicCoreInstructionKind)i;
        bool a = minic_core_instruction_is_integer_scalar(kind);
        bool b = minic_core_instruction_is_floating_scalar(kind);
        bool c = minic_core_instruction_is_call(kind);
        if ((unsigned)a + (unsigned)b + (unsigned)c > 1U) return 3;
        integer += a ? 1U : 0U;
        floating += b ? 1U : 0U;
        calls += c ? 1U : 0U;
    }
    if (integer != 20U || floating != 17U || calls != 2U ||
        minic_core_instruction_is_integer_scalar(
            MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW) ||
        minic_core_instruction_is_integer_scalar(
            MINIC_CORE_INSTRUCTION_SCALAR_BITCAST) ||
        minic_core_instruction_is_integer_scalar(
            MINIC_CORE_INSTRUCTION_ATOMIC_LOAD) ||
        minic_core_instruction_is_floating_scalar(
            MINIC_CORE_INSTRUCTION_VARIADIC_COPY) ||
        !minic_core_instruction_is_call(MINIC_CORE_INSTRUCTION_CALL) ||
        !minic_core_instruction_is_call(MINIC_CORE_INSTRUCTION_INDIRECT_CALL))
        return 4;
    puts("MINIC_FIVE_TARGET_CORE_FAMILY=PASS integer=20 float=17 call=2");
    return 0;
}
