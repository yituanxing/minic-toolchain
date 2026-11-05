#include "target/riscv64/runtime_libcall.h"
#include "target/riscv/instruction_select.h"

#include <stddef.h>

bool minic_riscv64_needs_machine_runtime_call(
    const MinicRiscvIsa *isa, const MinicCoreFunction *function) {
    size_t i;
    if (isa == NULL || isa->xlen != 64U || function == NULL ||
        (function->instruction_count != 0U && function->instructions == NULL))
        return false;
    for (i = 0U; i < function->instruction_count; ++i)
        if (minic_riscv_machine_implicit_runtime_call(
                function, &function->instructions[i], isa->xlen))
            return true;
    return false;
}
