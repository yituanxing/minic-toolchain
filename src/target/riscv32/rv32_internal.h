#ifndef MINIC_TARGET_RISCV32_INTERNAL_H
#define MINIC_TARGET_RISCV32_INTERNAL_H
#include "core/core_ir.h"
#include "target/common/core_function_facts.h"
#include "target/common/core_object_storage.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"
#include "target/riscv32/rv32_abi_plan.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* RV32I/ILP32 physical-machine contract: Core IR never owns frame slots,
 * integer-register placement, scratch-register choices or ABI stack. */
typedef struct Rv32Frame {
    /* Per-invocation RISC-V feature profile. The emitted C operation can
     * always fall back to the Base-I runtime helper when M is unavailable.
     * No assembler feature ownership is inferred from these bits. */
    const MinicRiscvIsa *isa;
    /* Outgoing ILP32 stack arguments are pre-reserved at sp+0 on every call.
     * This keeps sp 16-byte aligned, including nested GNU/LLVM library calls,
     * and avoids moving the Core SSA/local object frame during calls. */
    size_t outgoing_size;
    size_t frame_size;
    /* RV64-aligned-frame contract mirrored with an ILP32 4-byte entry SP. */
    bool has_dynamic_stack_alignment;
    size_t stack_alignment;
    size_t entry_sp_offset;
    /* Saved hidden sret pointer for record returns larger than 2*XLEN. */
    size_t sret_offset;
    size_t saved_base_offset; /* caller's s0 in dynamic-frame functions */
    bool has_dynamic_stack;
    bool is_variadic;
    size_t varargs_size;
    size_t varargs_offset;
    size_t *object_offsets;
    size_t *value_offsets;
    MinicCoreObjectStoragePlan object_storage;
    bool borrows_object_storage;
    size_t object_count;
    size_t value_count;
    /* ABI is planned before frame allocation; outgoing area size and every
     * machine CALL consume this one verified immutable call layout. */
    Rv32FunctionAbiPlan abi;
} Rv32Frame;

/* Frame ownership: rv32_frame.c */
bool rv32_frame_init_with_storage(Rv32Frame *, const MinicC0Program *,
    const MinicCoreFunction *, const MinicTargetInfo *, bool,
    const MinicCoreFunctionFacts *, const MinicCoreObjectStoragePlan *);
void rv32_frame_destroy(Rv32Frame *);
bool rv32_stack(FILE *, const char *, const char *, size_t);
bool rv32_fixed_stack(FILE *, const Rv32Frame *, const char *, const char *, size_t);
bool rv32_slot(FILE *, const Rv32Frame *, MinicCoreValueId, const char *, bool);
bool rv32_pair_slot(FILE *, const Rv32Frame *, MinicCoreValueId,
                    const char *, const char *, bool);
bool rv32_address(FILE *, const Rv32Frame *, const char *, size_t);
bool rv32_incoming_word(FILE *, const Rv32Frame *, size_t, const char *);
bool rv32_outgoing_word(FILE *, const Rv32Frame *, size_t, const char *);

/* Type widths and scalar transport: rv32_scalar.c */
bool rv32_scalar_storage_words(const MinicCoreFunction *, MinicType, unsigned *);
bool rv32_scalar(const MinicCoreFunction *, MinicType, unsigned *, bool *);
bool rv32_canon(FILE *, const MinicCoreFunction *, MinicType, const char *);
bool rv32_load_mem(FILE *, const MinicCoreFunction *, MinicType,
                   const char *, size_t);
bool rv32_store_mem(FILE *, const MinicCoreFunction *, MinicType, size_t);
bool rv32_load2(FILE *, const Rv32Frame *, const MinicCoreInstruction *);

/* Typed machine family lowering; all transport stays behind Target. */
bool rv32_emit_integer_instruction(FILE *, const MinicC0Program *,
                                   const MinicTargetInfo *, const MinicCoreFunction *,
                                   const Rv32Frame *, const MinicCoreInstruction *,
                                   const char *);
bool rv32_is_wide_integer(const MinicCoreFunction *, MinicType);
bool rv32_emit_wide_integer(FILE *, const MinicCoreFunction *,
                            const Rv32Frame *, const MinicCoreInstruction *,
                            const char *);
bool rv32_emit_fp_instruction(FILE *, const MinicCoreFunction *,
                              const Rv32Frame *, const MinicCoreInstruction *);
bool rv32_emit_memory_instruction(FILE *, const MinicC0Program *,
                                  const MinicTargetInfo *, const MinicCoreFunction *,
                                  const Rv32Frame *, const MinicCoreInstruction *);

/* Parameters/direct/indirect ABI lowering: rv32_call.c */
bool rv32_emit_parameter(FILE *, const MinicCoreFunction *,
                         const Rv32Frame *, const MinicCoreInstruction *);
bool rv32_emit_parameter_object(FILE *, const MinicCoreFunction *,
                                const Rv32Frame *, const MinicCoreInstruction *);
bool rv32_emit_record_return(FILE *, const Rv32Frame *,
                             const MinicCoreTerminator *, const char *);
/* Shared tail-safe word packing for Core record OBJECT arguments/returns. */
bool rv32_emit_object_word(FILE *, const Rv32Frame *, MinicCoreObjectId,
                           size_t, unsigned, const char *);
bool rv32_emit_call(FILE *, const MinicCoreFunction *,
                    const Rv32Frame *, const MinicCoreInstruction *,
                    MinicCoreInstructionId);
#endif
