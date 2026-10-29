#ifndef MINIC_TARGET_COMMON_DIRECT_FUNCTION_PROFILE_H
#define MINIC_TARGET_COMMON_DIRECT_FUNCTION_PROFILE_H

#include "target/common/direct_call_plan.h"
#include "target/common/core_function_facts.h"

/* This profile is shared only by the two GNU/QEMU-validated direct LP64
 * emitters. It is not an RV64 or x86 SysV ABI contract. */
typedef struct MinicDirectLP64FunctionProfile {
    size_t variadic_address_count;
    size_t fixed_gp_count;
    size_t fixed_fp_count;
    bool has_dynamic_stack;
} MinicDirectLP64FunctionProfile;

typedef enum MinicDirectLP64ProfileResult {
    MINIC_DIRECT_LP64_PROFILE_OK = 0,
    MINIC_DIRECT_LP64_PROFILE_VARIADIC_FIXED_FLOAT,
    MINIC_DIRECT_LP64_PROFILE_VARIADIC_SHAPE,
    MINIC_DIRECT_LP64_PROFILE_PARAMETER_SHAPE
} MinicDirectLP64ProfileResult;

/* Preserve the existing per-ABI variadic admissions, parameter type gates,
 * and incoming register/stack locations. No machine instructions are emitted. */
MinicDirectLP64ProfileResult minic_multi_direct_lp64_profile_function(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicDirectLP64AbiPolicy *abi,
    MinicDirectLP64IndirectRecord indirect_record,
    MinicLP64ArgumentPlan *incoming, MinicDirectLP64FunctionProfile *profile);

#endif
