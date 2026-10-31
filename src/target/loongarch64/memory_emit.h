#ifndef MINIC_TARGET_LOONGARCH64_MEMORY_EMIT_H
#define MINIC_TARGET_LOONGARCH64_MEMORY_EMIT_H
#include "core/core_ir.h"
#include "frontend/ast.h"
#include "target/common/lp64_frame.h"
#include "target/target_info.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Native Core memory loads/stores and relaxed atomic byte transfers. */
bool minic_loongarch64_emit_memory_instruction(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame_plan,
    size_t function_id, bool dynamic_frame, bool value_in_previous_gp,
    bool *needs_gp_store);

/* Outlined byte-accurate slow paths are local to each emitted function.
 * They never call through $ra and therefore work for leaf functions too. */
void minic_loongarch64_memory_helpers_begin(size_t function_id);
bool minic_loongarch64_memory_helpers_end(FILE *out);

/* Exact Core-origin/address/alignment eligibility used by the memory
 * emitter and address-materialization elision. Never elide a producer
 * unless every consumer is certified to take the direct native path. */
bool minic_loongarch64_direct_frame_memory_use(
    const MinicC0Program *, const MinicTargetInfo *,
    const MinicCoreFunction *, const MinicLP64FramePlan *,
    const MinicCoreInstruction *);

/* Store a compiler-materialized structured ASM register output through the
 * same alignment proof and native/byte-fallback transport as ordinary Core
 * stores. The chosen scratch register must not alias value or address. */
bool minic_loongarch64_emit_asm_output_store(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address,
    size_t width, const char *value_reg, const char *address_reg,
    const char *scratch_reg);

/* Read a compiler-owned +r ASM lvalue with the ordinary Core scalar
 * ingress semantics. Must run before loading other live target operands:
 * the byte-gather fallback uses $t2-$t5 and the address in $t1. */
bool minic_loongarch64_emit_asm_input_scalar(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, MinicCoreValueId address, size_t width);
#endif
