#include "target/aarch64/block_emit.h"
#include "target/aarch64/memory_emit.h"
#include "target/aarch64/call_emit.h"
#include "target/aarch64/integer_emit.h"
#include "target/aarch64/fp_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include "target/aarch64/abi.h"
#include "target/data_layout.h"
#include "target/common/direct_program_emit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The instruction-owned aggregate ingress and CFG terminator cases are
 * independent of the function-level preflight and frame lifetime. Machine
 * register syntax remains strictly owned by this ISA. */

bool minic_aarch64_emit_lp64_record_chunk_parameter(FILE *out, size_t offset,
                                              size_t bytes, bool in_reg,
                                              size_t gp, size_t incoming_offset) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  movz x11, #%zu\n", offset & 65535U) < 0 ||
        (offset > 65535U &&
         fprintf(out, "  movk x11, #%zu, lsl #16\n", offset >> 16U) < 0) ||
        fputs("  add x11, sp, x11\n", out) == EOF ||
        (in_reg
         ? fprintf(out, "  mov x9, x%zu\n", gp) < 0
         : fprintf(out, "  ldr x9, [sp, #%zu]\n", incoming_offset) < 0))
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  strb w9, [x11, #%zu]\n", b) < 0 ||
            (b + 1U < bytes && fputs("  lsr x9, x9, #8\n", out) == EOF))
            return false;
    return true;
}

bool minic_aarch64_emit_record_copy(FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins) {
const MinicCoreValueId dst = ins->value.record_copy.destination_address;
            const MinicCoreValueId src = ins->value.record_copy.source_address;
            size_t bytes;
            if (!minic_multi_direct_record_copy_extent(program, target, fn, ins, &bytes))
                return false;
            if (fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr x11, [sp, #%zu]\n",
                        (size_t)dst * 8U, (size_t)src * 8U) < 0)
                return false;
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ldrb w9, [x11]\n"
                          "  strb w9, [x10]\n"
                          "  add x11, x11, #1\n"
                          "  add x10, x10, #1\n", out) == EOF)
                    return false;
            return true;
}

bool minic_aarch64_emit_parameter_object(FILE *out,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan, size_t frame) {
const MinicLP64ArgumentLocation *location;
            size_t size, slots, object_offset, k, chunk;
            if (!minic_multi_direct_parameter_object_location(
                    fn, ins, incoming_plan, frame_plan, &location, &object_offset))
                return false;
            size = location->size_bytes;
            slots = location->kind == MINIC_LP64_ARGUMENT_RECORD
                ? location->chunks : 0U;
            if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
                /* Composite by-reference parameter: copy the caller-owned
                 * bytes to this callee's independently addressable object. */
                if ((location->gp_registers != 0U
                     ? fprintf(out, "  mov x10, x%zu\n", location->gp_first) < 0
                     : fprintf(out, "  ldr x10, [sp, #%zu]\n",
                               frame + location->stack_first * 8U) < 0) ||
                    fprintf(out, "  movz x11, #%zu\n",
                            object_offset & 65535U) < 0 ||
                    (object_offset > 65535U &&
                     fprintf(out, "  movk x11, #%zu, lsl #16\n",
                             object_offset >> 16U) < 0) ||
                    fputs("  add x11, sp, x11\n", out) == EOF)
                    return false;
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ldrb w9, [x10], #1\n"
                              "  strb w9, [x11], #1\n", out) == EOF)
                        return false;
                }
                return true;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const bool in_reg = chunk < location->gp_registers;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!minic_aarch64_emit_lp64_record_chunk_parameter(out,
                        object_offset + chunk * 8U, payload,
                        in_reg, in_reg ? location->gp_first + chunk : 0U,
                        in_reg ? 0U : frame +
                                     (location->stack_first + chunk) * 8U))
                    return false;
            }
            return true;
}

bool minic_aarch64_emit_terminator(FILE *out,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreBlock *block,
    size_t id, size_t block_id, const MinicLP64FramePlan *frame_plan,
    size_t frame, size_t save_offset, bool has_direct_calls,
    bool dynamic_frame) {
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_long_double(fn->return_type)) {
                size_t src;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, block->terminator.return_value, &src) ||
                    fprintf(out, "  ldr q0, [sp, #%zu]\n", src) < 0)
                    return false;
            }
            if ((!minic_type_is_void(fn->return_type) &&
                 !minic_type_is_long_double(fn->return_type) &&
                 fprintf(out, minic_type_is_double(fn->return_type)
                                  ? "  ldr d0, [sp, #%zu]\n"
                                  : minic_type_is_float(fn->return_type)
                                      ? "  ldr s0, [sp, #%zu]\n"
                                  : minic_lp64_register64_type(program, target, fn->return_type)
                                      ? "  ldr x0, [sp, #%zu]\n"
                                      : "  ldr w0, [sp, #%zu]\n",
                         (size_t)block->terminator.return_value * 8U) < 0) ||
                (has_direct_calls && fprintf(out, "  ldr x30, [sp, #%zu]\n",
                                             save_offset) < 0) ||
                (dynamic_frame &&
                 (fputs("  mov sp, x19\n", out) == EOF ||
                  fprintf(out, "  ldr x19, [sp, #%zu]\n",
                          save_offset + (has_direct_calls ? 8U : 0U)) < 0)) ||
                (frame != 0U &&
                 (fprintf(out, "  movz x12, #%zu\n", frame & 65535U) < 0 ||
                  (frame > 65535U &&
                   fprintf(out, "  movk x12, #%zu, lsl #16\n", frame >> 16U) < 0) ||
                  fputs("  add sp, sp, x12\n", out) == EOF)) ||
                fputs("  ret\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (fprintf(out, "  ldr x9, [sp, #%zu]\n  br x9\n",
                        (size_t)block->terminator.indirect_target * 8U) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  brk #0\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            const MinicCoreValueId cond = block->terminator.conditional.condition;
            const bool wide = minic_lp64_register64_type(program, target, fn->values[cond].type);
            /* Large computed-goto VM CFGs exceed cbnz's branch reach. */
            if (fn->block_count > 256U) {
                if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                     : "  ldr w9, [sp, #%zu]\n",
                            (size_t)cond * 8U) < 0 ||
                    fprintf(out, wide
                            ? "  cbz x9, .Lmini_arm_f%zu_false_%zu\n"
                            : "  cbz w9, .Lmini_arm_f%zu_false_%zu\n",
                            id, block_id) < 0 ||
                    fprintf(out, "  b .Lmini_arm_f%zu_b%u\n"
                                 ".Lmini_arm_f%zu_false_%zu:\n"
                                 "  b .Lmini_arm_f%zu_b%u\n",
                            id, (unsigned int)block->terminator.conditional.when_true,
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_false) < 0)
                    return false;
                break;
            }
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)cond * 8U) < 0 ||
                fprintf(out, wide
                            ? "  cbnz x9, .Lmini_arm_f%zu_b%u\n"
                              "  b .Lmini_arm_f%zu_b%u\n"
                            : "  cbnz w9, .Lmini_arm_f%zu_b%u\n"
                              "  b .Lmini_arm_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return false;
            break;
        }
        default: return false;
        }
    return true;
}

/* All Core blocks and their machine instruction cases are target-owned. */
static bool emit_integer_normalize(FILE *out, size_t width, bool sign) {
    const char *op = width == 1U
        ? (sign ? "  sxtb w9, w9\n" :
                  "  uxtb w9, w9\n")
        : width == 2U
            ? (sign ? "  sxth w9, w9\n" :
                      "  uxth w9, w9\n")
            : width == 4U ? "" : NULL;
    return op != NULL && fputs(op, out) != EOF;
}

/* Source-driven tinyAES M4 frontier. Reuse existing target DataLayout to
 * size ordinary integer globals and flat arrays, but keep ELF emission
 * in the ISA backend. Unsupported aggregate/relocation cases must fail
 * closed, rather than silently manufacture incorrect object bytes. */
/* cJSON's first original file-scope globals are a flat error record
 * {null pointer, zero size_t} and a function-pointer hooks record.
 * Reuse the validated shared target DataLayout for *byte offsets* and
 * function/object relocation addends. Only bounded flat scalar records
 * belong here; recursive records/unions/bitfields fail closed. */




/* Emission of target-neutral ELF STV_* binding metadata. */



/* External default-visibility ELF symbols may be interposed by glibc.
 * AArch64 ADR_PREL_PG_HI21 cannot address optind@@GLIBC from a PIE. Use
 * GOT for external data/code references. Keep only proven TU-local static
 * definitions on the already-tested direct ADRP path (AES RoundKey). */
static bool aarch64_static_object(const MinicC0Program *program,
                                  const char *symbol) {
    size_t i;
    if (program == NULL || symbol == NULL) return false;
    for (i = 0U; i < program->global_object_count; ++i) {
        const MinicGlobalObject *obj = &program->global_objects[i];
        if (obj->name != NULL && strcmp(obj->name, symbol) == 0 &&
            obj->is_internal && !obj->is_extern &&
            !obj->is_block_scope_extern_only) return true;
    }
    return false;
}

static bool aarch64_static_function(const MinicC0Program *program,
                                    const char *symbol) {
    size_t i;
    if (program == NULL || symbol == NULL) return false;
    for (i = 0U; i < program->function_count; ++i) {
        const MinicFunction *fn = &program->functions[i];
        const char *name = fn->assembler_name != NULL
            ? fn->assembler_name : fn->name;
        if (name != NULL && strcmp(name, symbol) == 0 &&
            fn->is_defined && fn->is_internal) return true;
    }
    return false;
}



/* M7: retain x86's established DataLayout-owned object sizing instead
 * of guessing eight bytes per Core object.  The new-ISA stack slots
 * are SP-positive; naturally aligned objects are laid out after SSA
 * value slots.  Keep bounded <= 8 KiB frames until large-offset ABI
 * access has been independently checked on both targets. */





/* Preserve 16-byte long-double SSA values outside 8-byte scalar slots. */


/* Target-local final legalization of out-of-range SP-relative memory access.
 * Full Core SSA stack slots are preserved, and x12 is a reserved scratch.
 * This pass does not transform small-frame GNU instructions. */



/* All Core GP-producing instruction families share this final slot
 * transport. The F/D arithmetic and ABI remain native and may opt into
 * binary64-width writeback without affecting integer ALU. */
static bool aarch64_emit_gp_result(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreInstruction *ins, size_t offset, bool include_double) {
    const bool wide = minic_lp64_register64_type(program, target, ins->type) ||
                      (include_double && minic_type_is_double(ins->type));
    return fprintf(out, wide ? "  str x9, [sp, #%zu]\n" :
                               "  str w9, [sp, #%zu]\n", offset) >= 0;
}

bool minic_aarch64_emit_core_blocks(FILE *out, const MinicDirectLP64BlockContext *ctx) {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    const char *name, *reject_stage;
    const MinicCoreBlock *block;
    size_t id, frame, save_offset, va_count, va_gp, i;
    size_t va_base, va_fp;
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX;
    bool has_direct_calls, dynamic_frame;
    if (out == NULL || ctx == NULL || ctx->program == NULL ||
        ctx->target == NULL || ctx->fn == NULL || ctx->frame_plan == NULL ||
        ctx->incoming_plan == NULL || ctx->name == NULL) return false;
    program = ctx->program;
    target = ctx->target;
    fn = ctx->fn;
    frame_plan = ctx->frame_plan;
    incoming_plan = ctx->incoming_plan;
    name = ctx->name;
    id = ctx->id;
    frame = ctx->frame;
    save_offset = ctx->save_offset;
    va_count = ctx->va_count;
    va_gp = ctx->va_gp;
    va_base = ctx->va_base;
    va_fp = ctx->va_fp;
    has_direct_calls = ctx->has_direct_calls;
    dynamic_frame = ctx->dynamic_frame;
    reject_stage = "machine_emit";
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        reject_block = block_id;
        reject_ins = SIZE_MAX;
        reject_stage = "machine_emit";
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_arm_f%zu_b%zu:\n", id, block_id) < 0 ||
            (block->source_label_id != SIZE_MAX &&
             !fn->suppress_source_label_aliases &&
             fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0))
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        MinicDirectMachineInstructionShape shape;
        if (ins_id >= fn->instruction_count) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        ins = &fn->instructions[ins_id];
        reject_ins = (size_t)ins_id;
        if (!minic_multi_direct_machine_instruction_shape(
                fn, ins, true, &shape))
            return minic_multi_direct_machine_reject(
                fn, "aarch64", name, reject_stage,
                reject_block, reject_ins, __LINE__);
        offset = shape.result_offset;
        /* A Core compiler barrier is an effect-only ordering constraint
         * (GNU empty asm with a memory clobber), not a hardware fence and
         * never an SSA value. Like the existing x86/RV64 backends, preserve
         * source instruction order and emit no machine instruction. The
         * previous result-slot gate rejected this valid Core kind in the
         * original BusyBox test/gzip/netstat/top applets on both ISAs. */
        if (shape.family == MINIC_DIRECT_MACHINE_COMPILER_BARRIER)
            continue;
        /* M23: copy the *exact* target-sized aggregate bytes between Core
         * addresses, preserving padding and nested hooks (no re-layout).
         * An effect-only record copy has no SSA result slot. */
        if (shape.family == MINIC_DIRECT_MACHINE_RECORD_COPY) {
            if (!minic_aarch64_emit_record_copy(out, program, target, fn, ins))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (shape.family == MINIC_DIRECT_MACHINE_PARAMETER_OBJECT) {
            if (!minic_aarch64_emit_parameter_object(out, fn, ins,
                    incoming_plan, frame_plan, frame))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Preserve both halves of volatile/nonvolatile int128 stores.
         * Unlike atomic stores this is two plain 64-bit memory writes. */
        /* Memory effects precede the ordinary SSA gate: stores are effect
         * only, loads either produce a scalar GP value or a wide slot. */
        if (shape.family == MINIC_DIRECT_MACHINE_MEMORY) {
            bool needs_gp_store = false;
            if (!minic_aarch64_emit_memory_instruction(
                    out, program, target, fn, ins, frame_plan,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
            if (needs_gp_store && !aarch64_emit_gp_result(
                    out, program, target, ins, offset, true))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        /* The Core result slot and family have already been admitted by
         * minic_multi_direct_machine_instruction_shape(). */
        /* The native FP instruction family is a single target-owned
         * emission unit; Core semantics are shared, machine opcodes are not.
         * GP-producing comparisons/conversions reuse normal writeback. */
        if (shape.family == MINIC_DIRECT_MACHINE_FP) {
            bool needs_gp_store = false;
            if (!minic_aarch64_emit_fp_instruction(
                    out, program, target, fn, ins, offset,
                    frame_plan,
                    emit_integer_normalize,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store && !aarch64_emit_gp_result(
                    out, program, target, ins, offset, true))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Integer and pointer ALU is a complete native instruction family.
         * Shared Core widths/shapes; ISA-dependent flags and bit arithmetic. */
        if (shape.family == MINIC_DIRECT_MACHINE_INTEGER) {
            bool needs_gp_store = false;
            if (!minic_aarch64_emit_integer_instruction(
                    out, program, target, fn, ins, frame_plan,
                    emit_integer_normalize, &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store && !aarch64_emit_gp_result(
                    out, program, target, ins, offset, false))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Calls and callee ingress form a single ABI transfer family.
         * The native emitter consumes the common argument location plan. */
        if (shape.family == MINIC_DIRECT_MACHINE_ABI) {
            bool needs_gp_store = false;
            if (!minic_aarch64_emit_abi_instruction(
                    out, ctx, ins, offset, emit_integer_normalize,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store && !aarch64_emit_gp_result(
                    out, program, target, ins, offset, true))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage,
                    reject_block, reject_ins, __LINE__);
            continue;
        }
        switch (ins->kind) {
        case MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT: {
            size_t width = 0U;
            bool sign = false;
            /* Match x86 Core constant semantics: the destination integer
             * type determines narrowing, not the original int64 payload.
             * Lua exercises out-of-range literals in cast/enum contexts. */
            (void)minic_lp64_scalar_integer_width(program, target, ins->type,
                                       &width, &sign);
            if (width == 8U) {
                /* Full 64-bit constant: preserve high halves AND sign bits. */
                const uint64_t bits = (uint64_t)ins->value.integer_value;
                unsigned int shift;
                if (fprintf(out, "  movz x9, #%" PRIu64 "\n",
                            bits & UINT64_C(0xffff)) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                for (shift = 16U; shift <= 48U; shift += 16U) {
                    if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                        fprintf(out, "  movk x9, #%" PRIu64 ", lsl #%u\n",
                                (bits >> shift) & UINT64_C(0xffff), shift) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                }
            } else {
                uint32_t bits;
                /* Non-integer Core pseudo-bools retain the old 32-bit
                 * conservative gate; real 8/16/32-bit values wrap to width. */
                if (width == 0U &&
                    (ins->value.integer_value < INT32_MIN ||
                     ins->value.integer_value > INT32_MAX)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                bits = (uint32_t)(int32_t)ins->value.integer_value;
                if (fprintf(out, "  movz w9, #%" PRIu32 "\n",
                            bits & UINT32_C(0xffff)) < 0 ||
                    ((bits >> 16U) != 0U &&
                     fprintf(out, "  movk w9, #%" PRIu32 ", lsl #16\n",
                             bits >> 16U) < 0)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                if ((width == 1U || width == 2U) &&
                    !emit_integer_normalize(out, width, sign))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            (void)sign;
            break;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS: {
            const char *symbol;
            if (ins->value.global_id >= fn->global_count ||
                fn->globals[ins->value.global_id].name == NULL)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            symbol = fn->globals[ins->value.global_id].name;
            if (aarch64_static_object(program, symbol)) {
                /* RoundKey and other proven static-TU objects: preserve
                 * direct static-PC-relative addresses. */
                if (fprintf(out, "  adrp x9, %s\n"
                                 "  add x9, x9, :lo12:%s\n",
                            symbol, symbol) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            } else {
                /* External libc optind and default-visible globals require
                 * GOT relocation in PIE. This also links in static ELF. */
                if (fprintf(out, "  adrp x9, :got:%s\n"
                                 "  ldr x9, [x9, :got_lo12:%s]\n",
                            symbol, symbol) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            }
            if (fprintf(out, "  str x9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const MinicCoreFunctionSymbolId symbol_id =
                ins->value.function_symbol_id;
            const char *symbol;
            if (symbol_id >= fn->function_symbol_count ||
                !minic_type_is_pointer(ins->type)) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            symbol = fn->function_symbols[symbol_id].name;
            if (symbol == NULL || symbol[0] == '\0')
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (aarch64_static_function(program, symbol)) {
                if (fprintf(out, "  adrp x9, %s\n"
                                 "  add x9, x9, :lo12:%s\n",
                            symbol, symbol) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            } else if (fprintf(out, "  adrp x9, :got:%s\n"
                                    "  ldr x9, [x9, :got_lo12:%s]\n",
                               symbol, symbol) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (fprintf(out, "  str x9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            size_t object_offset;
            if (ins->value.object_id >= fn->object_count ||
                !minic_lp64_frame_plan_object(frame_plan, (size_t)ins->value.object_id, &object_offset) ||
                /* Large Lua VM stack objects exceed the 16-bit movz
                 * immediate. Keep the exact DataLayout offset and
                 * materialize it in the same reserved x9 scratch. */
                fprintf(out, "  movz x9, #%zu\n",
                        object_offset & 65535U) < 0 ||
                (object_offset > 65535U &&
                 fprintf(out, "  movk x9, #%zu, lsl #16\n",
                         object_offset >> 16U) < 0) ||
                fputs("  add x9, sp, x9\n", out) == EOF ||
                fprintf(out, "  str x9, [sp, #%zu]\n", offset) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            /* Core already computed the DataLayout record byte displacement.
             * Large zlib structures exceed the ISA add-immediate range. */
            const MinicCoreValueId base = ins->value.field_address.base;
            const size_t bytes = ins->value.field_address.byte_offset;
            if (base >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n",
                        (size_t)base * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (bytes != 0U && bytes <= 4095U) {
                if (fprintf(out, "  add x9, x9, #%zu\n", bytes) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            } else if (bytes > 4095U) {
                const uint64_t imm = (uint64_t)bytes;
                unsigned int shift;
                if (fprintf(out, "  movz x10, #%" PRIu64 "\n",
                            imm & UINT64_C(0xffff)) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
                for (shift = 16U; shift <= 48U; shift += 16U) {
                    if (((imm >> shift) & UINT64_C(0xffff)) != 0U &&
                        fprintf(out, "  movk x10, #%" PRIu64 ", lsl #%u\n",
                                (imm >> shift) & UINT64_C(0xffff), shift) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                                   reject_block, reject_ins, __LINE__);
                }
                if (fputs("  add x9, x9, x10\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_POINTER_OFFSET: {
            const MinicCoreValueId base = ins->value.pointer_offset.base;
            const MinicCoreValueId index = ins->value.pointer_offset.index;
            const size_t stride = ins->value.pointer_offset.element_size;
            size_t iw;
            bool signed_index;
            if (getenv("MINIC_M1_TRACE") != NULL && base < fn->value_count &&
                index < fn->value_count) {
                unsigned int bits = 0U;
                bool have_bits = minic_target_info_integer_width(
                    target, program, fn->values[index].type, &bits);
                (void)fprintf(stderr,
                    "MINIC_M11_POINTER_DIAG arch=aarch64 function=%s block=%zu ins=%u base=%u base_pointer=%d index=%u index_integer=%d width_known=%d index_bits=%u stride=%zu subtract=%d\n",
                    name, block_id, (unsigned int)ins_id,
                    (unsigned int)base,
                    minic_type_is_pointer(fn->values[base].type) ? 1 : 0,
                    (unsigned int)index,
                    minic_type_is_integer(fn->values[index].type) ? 1 : 0,
                    have_bits ? 1 : 0, bits, stride,
                    ins->value.pointer_offset.subtract ? 1 : 0);
            }
            if (!minic_multi_direct_pointer_offset_shape(
                    program, target, fn, ins, 65535U, &iw, &signed_index) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n", (size_t)base * 8U) < 0 ||
                fprintf(out,
                        iw == 1U ? (signed_index
                                     ? "  ldrsb x10, [sp, #%zu]\n"
                                     : "  ldrb w10, [sp, #%zu]\n")
                        : iw == 2U ? (signed_index
                                     ? "  ldrsh x10, [sp, #%zu]\n"
                                     : "  ldrh w10, [sp, #%zu]\n")
                        : iw == 8U ? "  ldr x10, [sp, #%zu]\n"
                        : (signed_index
                                     ? "  ldrsw x10, [sp, #%zu]\n"
                                     : "  ldr w10, [sp, #%zu]\n"),
                        (size_t)index * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (stride != 0U) {
                if (fprintf(out, "  movz x11, #%zu\n"
                                 "  mul x10, x10, x11\n"
                                 "  %s x9, x9, x10\n", stride,
                            ins->value.pointer_offset.subtract ? "sub" : "add") < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_BITCAST: {
            const MinicCoreValueId source = ins->value.operand;
            size_t src_width = 0U;
            bool src_signed = false;
            bool src_pointer = false;
            bool pointer_to_integer = false;
            if (!minic_multi_direct_scalar_bitcast_shape(
                    program, target, fn, ins, &pointer_to_integer,
                    &src_width, &src_signed, &src_pointer))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (pointer_to_integer) {
                if (fprintf(out, "  ldr x9, [sp, #%zu]\n",
                            (size_t)source * 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (fprintf(out, (src_pointer || src_width == 8U)
                          ? "  ldr x9, [sp, #%zu]\n"
                          : "  ldr w9, [sp, #%zu]\n",
                        (size_t)source * 8U) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!src_pointer && src_width != 8U &&
                (!emit_integer_normalize(out, src_width, src_signed) ||
                 fputs(src_signed ? "  sxtw x9, w9\n"
                                  : "  uxtw x9, w9\n", out) == EOF))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_NEXT_SCALAR: {
            /* AAPCS64 va_list: {__stack, __gr_top, __vr_top,
             * __gr_offs, __vr_offs}. Read from the correct register bank
             * (GP slots = 8 bytes, FP slots = 16 bytes) or from the
             * incoming overflow stack, then update the mutable cursor. */
            const bool fp = minic_type_is_double(ins->type);
            size_t width = 8U;
            bool sign = false;
            if (ins->result >= fn->value_count ||
                ins->value.operand >= fn->value_count ||
                !minic_type_is_pointer(
                    fn->values[ins->value.operand].type) ||
                (!fp && !minic_type_is_pointer(ins->type) &&
                 !minic_lp64_scalar_integer_width(program, target, ins->type,
                                       &width, &sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fprintf(out,
                        "  ldr x9, [sp, #%zu]\n"
                        "  ldrsw x10, [x9, #%u]\n"
                        "  cmp w10, #0\n"
                        "  b.ge .Lmini_arm_va_f%zu_r%u_stack\n"
                        "  ldr x11, [x9, #%u]\n"
                        "  add x11, x11, x10\n"
                        "  ldr x13, [x11]\n"
                        "  add w10, w10, #%u\n"
                        "  str w10, [x9, #%u]\n"
                        "  b .Lmini_arm_va_f%zu_r%u_done\n"
                        ".Lmini_arm_va_f%zu_r%u_stack:\n"
                        "  ldr x11, [x9]\n"
                        "  ldr x13, [x11]\n"
                        "  add x11, x11, #8\n"
                        "  str x11, [x9]\n"
                        ".Lmini_arm_va_f%zu_r%u_done:\n"
                        "  mov x9, x13\n",
                        (size_t)ins->value.operand * 8U,
                        fp ? 28U : 24U,
                        id, (unsigned int)ins->result,
                        fp ? 16U : 8U,
                        fp ? 16U : 8U,
                        fp ? 28U : 24U,
                        id, (unsigned int)ins->result,
                        id, (unsigned int)ins->result,
                        id, (unsigned int)ins->result) < 0 ||
                (!fp && !minic_type_is_pointer(ins->type) &&
                 width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
            const size_t stack_top = frame;
            const size_t gr_top = va_base + 96U;
            const size_t vr_top = va_base + 224U;
            if (va_count != 1U || !minic_type_is_pointer(ins->type) ||
                ins->result >= fn->value_count)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            /* Linux AAPCS64 va_list layout:
             * void *__stack, *__gr_top, *__vr_top; int __gr_offs, __vr_offs.
             * The GP and FP save areas are owned by our entry prologue. */
            if (fprintf(out, "  movz x9, #%zu\n", va_base & 65535U) < 0 ||
                (va_base > 65535U &&
                 fprintf(out, "  movk x9, #%zu, lsl #16\n",
                         va_base >> 16U) < 0) ||
                fputs("  add x9, sp, x9\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (fprintf(out, "  movz x10, #%zu\n",
                        stack_top & 65535U) < 0 ||
                (stack_top > 65535U &&
                 fprintf(out, "  movk x10, #%zu, lsl #16\n",
                         stack_top >> 16U) < 0) ||
                fputs("  add x10, sp, x10\n  str x10, [x9]\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (fprintf(out, "  movz x10, #%zu\n", gr_top & 65535U) < 0 ||
                (gr_top > 65535U &&
                 fprintf(out, "  movk x10, #%zu, lsl #16\n", gr_top >> 16U) < 0) ||
                fputs("  add x10, sp, x10\n  str x10, [x9, #8]\n", out) == EOF ||
                fprintf(out, "  movz x10, #%zu\n", vr_top & 65535U) < 0 ||
                (vr_top > 65535U &&
                 fprintf(out, "  movk x10, #%zu, lsl #16\n", vr_top >> 16U) < 0) ||
                fputs("  add x10, sp, x10\n  str x10, [x9, #16]\n", out) == EOF ||
                fprintf(out, va_gp == 8U
                            ? "  movz w10, #0\n"
                            : "  movn w10, #%zu\n",
                        va_gp == 8U ? 0U : (8U - va_gp) * 8U - 1U) < 0 ||
                fputs("  str w10, [x9, #24]\n", out) == EOF ||
                fprintf(out, va_fp == 8U
                            ? "  movz w10, #0\n"
                            : "  movn w10, #%zu\n",
                        va_fp == 8U ? 0U : (8U - va_fp) * 16U - 1U) < 0 ||
                fputs("  str w10, [x9, #28]\n", out) == EOF ||
                fprintf(out, "  movz x9, #%zu\n", va_base & 65535U) < 0 ||
                (va_base > 65535U &&
                 fprintf(out, "  movk x9, #%zu, lsl #16\n",
                         va_base >> 16U) < 0) ||
                fputs("  add x9, sp, x9\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC: {
            const MinicCoreValueId count = ins->value.operand;
            if (!dynamic_frame || ins->result >= fn->value_count ||
                count >= fn->value_count || !minic_type_is_pointer(ins->type) ||
                !minic_type_equal(fn->values[ins->result].type, ins->type) ||
                !minic_type_equal(fn->values[count].type,
                                  minic_type_unsigned_long()) ||
                fprintf(out,
                    "  ldr x9, [sp, #%zu]\n"
                    "  adds x10, x9, #15\n"
                    "  b.cs .Lmini_arm_alloc_overflow_%zu_%u\n"
                    "  and x10, x10, #-16\n"
                    "  sub sp, sp, x10\n"
                    "  mov x9, sp\n"
                    "  b .Lmini_arm_alloc_done_%zu_%u\n"
                    ".Lmini_arm_alloc_overflow_%zu_%u:\n"
                    "  brk #0\n"
                    ".Lmini_arm_alloc_done_%zu_%u:\n",
                    (size_t)count * 8U,
                    id, (unsigned int)ins_id,
                    id, (unsigned int)ins_id,
                    id, (unsigned int)ins_id,
                    id, (unsigned int)ins_id) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
            const MinicCoreValueId src = ins->value.operand;
            size_t src_width, dst_width;
            bool src_sign, dst_sign;
            /* RV64-proven pair model: keep int128 as low/high 64-bit halves,
             * in isolated 16-byte SSA slots. No silent XLEN truncation. */
            if (src < fn->value_count &&
                (minic_type_is_int128_integer(ins->type) ||
                 minic_type_is_int128_integer(fn->values[src].type))) {
                const bool src128 = minic_type_is_int128_integer(fn->values[src].type);
                const bool dst128 = minic_type_is_int128_integer(ins->type);
                size_t src_offset = 0U, dst_offset = 0U;
                if (src128) {
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, src, &src_offset) ||
                        fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                     "  ldr x10, [sp, #%zu]\n",
                                src_offset, src_offset + 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else {
                    if (!minic_lp64_scalar_integer_width(program, target,
                            fn->values[src].type, &src_width, &src_sign) ||
                        fprintf(out, src_width == 8U ?
                                "  ldr x9, [sp, #%zu]\n" :
                                "  ldr w9, [sp, #%zu]\n", (size_t)src * 8U) < 0 ||
                        (src_width < 8U &&
                         (!emit_integer_normalize(out, src_width, src_sign) ||
                          (src_sign && fputs("  sxtw x9, w9\n", out) == EOF))) ||
                        fputs(src_sign ? "  asr x10, x9, #63\n" :
                                         "  mov x10, xzr\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                }
                if (dst128) {
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst_offset) ||
                        fprintf(out, "  str x9, [sp, #%zu]\n"
                                     "  str x10, [sp, #%zu]\n",
                                dst_offset, dst_offset + 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                    continue;
                }
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                           &dst_width, &dst_sign) ||
                    (dst_width < 8U &&
                     !emit_integer_normalize(out, dst_width, dst_sign)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                break;
            }
            /* Inventory both source/destination C integer widths before
             * admitting a cast. The SQLite 128-bit helper includes a type
             * conversion outside the already-proven 32/64 scalar subset. */
            if (getenv("MINIC_BACKEND_CENSUS") != NULL) {
                size_t source_bytes = 0U, dest_bytes = 0U;
                bool source_sign = false, dest_sign = false;
                const bool source_ok = src < fn->value_count &&
                    minic_lp64_scalar_integer_width(program, target,
                        fn->values[src].type, &source_bytes, &source_sign);
                const bool dest_ok = minic_lp64_scalar_integer_width(
                    program, target, ins->type, &dest_bytes, &dest_sign);
                if (!source_ok || !dest_ok)
                    (void)fprintf(stderr,
                        "MINIC_SQLITE_CONVERSION_LIMIT arch=aarch64 function=%s"
                        " ins=%zu source_ok=%d src_bytes=%zu src_sign=%d"
                        " dest_ok=%d dst_bytes=%zu dst_sign=%d"
                        " source_kind=%d dest_kind=%d\n",
                        name, (size_t)ins_id, source_ok, source_bytes, source_sign,
                        dest_ok, dest_bytes, dest_sign,
                        src < fn->value_count
                            ? (int)fn->values[src].type.base_kind : -1,
                        (int)ins->type.base_kind);
            }
            if (src >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target,
                                      fn->values[src].type, &src_width, &src_sign) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type,
                                      &dst_width, &dst_sign) ||
                fprintf(out, src_width == 8U
                            ? "  ldr x9, [sp, #%zu]\n"
                            : "  ldr w9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (dst_width == 8U) {
                if (src_width != 8U &&
                    (!emit_integer_normalize(out, src_width, src_sign) ||
                     (src_sign && fputs("  sxtw x9, w9\n", out) == EOF)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                /* Unsigned narrow source already zero-extends on w writes. */
            } else if (!emit_integer_normalize(out, dst_width, dst_sign))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_OPCODE_REJECT arch=aarch64 function=%s block=%zu instruction=%u kind=%d\n",
                              name, block_id, (unsigned int)ins_id, (int)ins->kind);
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
        if (!aarch64_emit_gp_result(out, program, target, ins, offset, true))
            return minic_multi_direct_machine_reject(
                fn, "aarch64", name, reject_stage,
                reject_block, reject_ins, __LINE__);
    }
        /* Branches use the function's own local ELF labels; x9 is a
         * scratch register and w0 is only defined along return paths. */
        reject_stage = "terminator_emit";
        reject_ins = SIZE_MAX;
        if (!minic_aarch64_emit_terminator(out, program, target, fn, block,
                id, block_id, frame_plan, frame, save_offset,
                has_direct_calls, dynamic_frame))
            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                reject_stage, reject_block, reject_ins, __LINE__);
    }
    return true;
}
