#ifndef MINIC_TARGET_X86_64_ABI_H
#define MINIC_TARGET_X86_64_ABI_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>

/* The one physical register-bank description used by SysV/Win64 argument
 * planners, incoming parameter materialization and outgoing CALL emission.
 * The GP result sequence is low/high for SysV INTEGER-class returns; it does
 * not imply that Win64 accepts the same aggregate or __int128 signatures. */
typedef struct MinicX86AbiRegisters {
    const char *const *gp_arguments;
    size_t gp_argument_count;
    size_t fp_argument_count;
    const char *const *gp_results;
    size_t gp_result_count;
    /* Registers additionally nonvolatile under this ABI beyond the SysV
     * callee-saved set. A Win64 definition may call arbitrary SysV code,
     * so the function prologue must preserve XMM6..15 and RDI/RSI. */
    const char *const *extra_nonvolatile_gp;
    size_t extra_nonvolatile_gp_count;
    size_t extra_nonvolatile_xmm_first;
    size_t extra_nonvolatile_xmm_count;
} MinicX86AbiRegisters;

const MinicX86AbiRegisters *minic_x86_64_abi_registers(
    MinicFunctionCallingConvention convention);

/* SysV x86-64 target-specific ABI classification. This interface does not
 * assert compatibility with the LP64 strategies used by other targets.
 * Machine instruction selection, stack slots and x87 ops stay private. */
bool minic_x86_64_scalar_width(const MinicC0Program *, const MinicTargetInfo *,
                               MinicType, size_t *, bool *);
bool minic_x86_64_storage_width(const MinicC0Program *, const MinicTargetInfo *,
                                MinicType, size_t *, bool *);
bool minic_x86_64_one_gp_record(const MinicC0Program *, const MinicTargetInfo *,
                                MinicType, size_t *);
bool minic_x86_64_two_gp_record(const MinicC0Program *, const MinicTargetInfo *, MinicType);
bool minic_x86_64_record_abi_compatible(MinicType, MinicType);
/* SysV AMD64 MEMORY-class result uses a hidden first GPR destination. */
bool minic_x86_64_memory_result_record(const MinicC0Program *,
                                      const MinicTargetInfo *, MinicType, size_t *);

#endif
