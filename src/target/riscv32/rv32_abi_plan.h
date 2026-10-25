#ifndef MINIC_TARGET_RISCV32_ABI_PLAN_H
#define MINIC_TARGET_RISCV32_ABI_PLAN_H

#include "target/common/core_call_view.h"
#include "target/riscv32/rv32_abi_cursor.h"
#include <stdbool.h>
#include <stddef.h>

/* An ABI location is computed ONCE per actual Core call argument and
 * incoming formal. Counts and stack indexes are in XLEN=32 words.
 * The plan owns its placement arrays; CoreCallView only borrows Core IR.
 * Frame sizing and emission consume exactly the same immutable plan. */
typedef struct Rv32AbiWordLocation {
    MinicRiscv32AbiPlacement location;
    unsigned words;
    /* For Core record objects: <=2 XLEN words by value, larger records
     * by reference to a caller-owned snapshot per the soft ILP32 psABI. */
    size_t record_size;
    bool indirect_record;
} Rv32AbiWordLocation;

typedef struct Rv32CallAbiPlan {
    MinicCoreCallView view;
    Rv32AbiWordLocation *arguments;
    size_t stack_words;
    bool prepared;
} Rv32CallAbiPlan;

typedef struct Rv32FunctionAbiPlan {
    Rv32AbiWordLocation *parameters;
    size_t parameter_count;
    size_t named_gpr_words;
    size_t named_stack_words;
    Rv32CallAbiPlan *calls; /* indexed by MinicCoreInstructionId */
    size_t call_count;
    size_t max_outgoing_stack_words;
} Rv32FunctionAbiPlan;

bool rv32_function_abi_plan_build(const MinicC0Program *,
                                  const MinicTargetInfo *,
                                  const MinicCoreFunction *,
                                  Rv32FunctionAbiPlan *);
void rv32_function_abi_plan_destroy(Rv32FunctionAbiPlan *);

/* A missing, non-call or out-of-range instruction always fails closed. */
const Rv32CallAbiPlan *rv32_function_abi_call(
    const Rv32FunctionAbiPlan *, MinicCoreInstructionId);
#endif
