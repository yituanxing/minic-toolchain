#ifndef MINIC_TARGET_RISCV32_INTERNAL_H
#define MINIC_TARGET_RISCV32_INTERNAL_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "minic/compiler.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* RV32I/ILP32 physical-machine contract: Core IR never owns frame slots,
 * integer-register placement, scratch-register choices or ABI stack. */
typedef struct Rv32Frame {
    /* Outgoing ILP32 stack arguments are pre-reserved at sp+0 on every call.
     * This keeps sp 16-byte aligned, including nested GNU/LLVM library calls,
     * and avoids moving the Core SSA/local object frame during calls. */
    size_t outgoing_size;
    size_t frame_size;
    size_t *object_offsets;
    size_t *value_offsets;
    size_t object_count;
    size_t value_count;
} Rv32Frame;

/* Frame ownership: rv32_frame.c */
bool rv32_frame_init(Rv32Frame *, const MinicC0Program *,
                     const MinicCoreFunction *, const MinicTargetInfo *);
void rv32_frame_destroy(Rv32Frame *);
bool rv32_stack(FILE *, const char *, const char *, size_t);
bool rv32_slot(FILE *, const Rv32Frame *, MinicCoreValueId, const char *, bool);
bool rv32_pair_slot(FILE *, const Rv32Frame *, MinicCoreValueId,
                    const char *, const char *, bool);
bool rv32_address(FILE *, const char *, size_t);
bool rv32_incoming_word(FILE *, const Rv32Frame *, size_t, const char *);
bool rv32_outgoing_word(FILE *, const Rv32Frame *, size_t, const char *);

/* Type widths and scalar transport: rv32_scalar.c */
bool rv32_scalar_storage_words(const MinicCoreFunction *, MinicType, unsigned *);
bool rv32_scalar(const MinicCoreFunction *, MinicType, unsigned *, bool *);
bool rv32_canon(FILE *, const MinicCoreFunction *, MinicType, const char *);
bool rv32_load_mem(FILE *, const MinicCoreFunction *, MinicType, const char *);
bool rv32_store_mem(FILE *, const MinicCoreFunction *, MinicType);
bool rv32_load2(FILE *, const Rv32Frame *, const MinicCoreInstruction *);

/* Typed machine family lowering; all transport stays behind Target. */
bool rv32_emit_integer_instruction(FILE *, const MinicCoreFunction *,
                                   const Rv32Frame *, const MinicCoreInstruction *);
bool rv32_emit_fp_instruction(FILE *, const MinicCoreFunction *,
                              const Rv32Frame *, const MinicCoreInstruction *);
bool rv32_emit_memory_instruction(FILE *, const MinicC0Program *,
                                  const MinicTargetInfo *, const MinicCoreFunction *,
                                  const Rv32Frame *, const MinicCoreInstruction *);

/* Parameters/direct/indirect ABI lowering: rv32_call.c */
bool rv32_emit_parameter(FILE *, const MinicCoreFunction *,
                         const Rv32Frame *, const MinicCoreInstruction *);
bool rv32_emit_call(FILE *, const MinicCoreFunction *,
                    const Rv32Frame *, const MinicCoreInstruction *);
#endif
