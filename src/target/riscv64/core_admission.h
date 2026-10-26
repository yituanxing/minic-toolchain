#ifndef MINIC_TARGET_RISCV64_CORE_ADMISSION_H
#define MINIC_TARGET_RISCV64_CORE_ADMISSION_H
#include "core/core_ir.h"
#include "target/riscv64/function_abi_plan.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_object_storage.h"
bool minic_riscv64_core_function_admit_profile(
    const MinicC0Program *, const MinicCoreFunction *,
    MinicRiscv64FunctionAbiPlan *, bool lp64_soft_float,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage);
bool minic_riscv64_core_function_admit(
    const MinicC0Program *, const MinicCoreFunction *,
    MinicRiscv64FunctionAbiPlan *);
#endif
