#include "target/aarch64/block_emit.h"
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
static bool emit_integer_memory(FILE *out, bool store,
                                size_t width, bool sign) {
    const char *op = NULL;
    if (store) {
        op = width == 1U ? "  strb w9, [x10]\n" :
             width == 2U ? "  strh w9, [x10]\n" :
             width == 4U ? "  str w9, [x10]\n" :
             width == 8U ? "  str x9, [x10]\n" : NULL;
    } else {
        op = width == 1U ? (sign ? "  ldrsb w9, [x10]\n" :
                                     "  ldrb w9, [x10]\n") :
             width == 2U ? (sign ? "  ldrsh w9, [x10]\n" :
                                     "  ldrh w9, [x10]\n") :
             width == 4U ? "  ldr w9, [x10]\n" :
              width == 8U ? "  ldr x9, [x10]\n" : NULL;
    }
    return op != NULL && fputs(op, out) != EOF;
}

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



/* One or two INTEGER-class ABI eightbytes. Serialize actual record bytes,
 * not a speculative 64-bit load across a partial object/array boundary.
 * x9-x11 are scratch; x12 remains reserved for large-SP legalization. */
static bool emit_lp64_record_chunk_argument(FILE *out, size_t offset,
                                             size_t bytes, bool in_reg,
                                             size_t gp, size_t stack_slot) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U)) return false;
    if (fprintf(out, "  movz x11, #%zu\n", offset & 65535U) < 0 ||
        (offset > 65535U &&
         fprintf(out, "  movk x11, #%zu, lsl #16\n", offset >> 16U) < 0) ||
        fputs("  add x11, sp, x11\n  mov x9, xzr\n", out) == EOF)
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  ldrb w10, [x11, #%zu]\n"
                         "  orr x9, x9, x10, lsl #%zu\n",
                    b, b * 8U) < 0) return false;
    return in_reg
        ? fprintf(out, "  mov x%zu, x9\n", gp) >= 0
        : fprintf(out, "  str x9, [sp, #%zu]\n", stack_slot * 8U) >= 0;
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
        if (ins_id >= fn->instruction_count) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        ins = &fn->instructions[ins_id];
        reject_ins = (size_t)ins_id;
        /* A Core compiler barrier is an effect-only ordering constraint
         * (GNU empty asm with a memory clobber), not a hardware fence and
         * never an SSA value. Like the existing x86/RV64 backends, preserve
         * source instruction order and emit no machine instruction. The
         * previous result-slot gate rejected this valid Core kind in the
         * original BusyBox test/gzip/netstat/top applets on both ISAs. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_COMPILER_BARRIER) {
            if (!minic_multi_direct_compiler_barrier_shape(ins))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        /* M23: copy the *exact* target-sized aggregate bytes between Core
         * addresses, preserving padding and nested hooks (no re-layout).
         * An effect-only record copy has no SSA result slot. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
            if (!minic_aarch64_emit_record_copy(out, program, target, fn, ins))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
            if (!minic_aarch64_emit_parameter_object(out, fn, ins,
                    incoming_plan, frame_plan, frame))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Preserve both halves of volatile/nonvolatile int128 stores.
         * Unlike atomic stores this is two plain 64-bit memory writes. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_int128_integer(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t src;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr x9, [sp, #%zu]\n"
                             "  ldr x11, [sp, #%zu]\n"
                             "  str x9, [x10]\n"
                             "  str x11, [x10, #8]\n",
                        (size_t)addr * 8U, src, src + 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_long_double(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t src;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n"
                             "  ldr q9, [sp, #%zu]\n"
                             "  str q9, [x10]\n",
                        (size_t)addr * 8U, src) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE ||
            ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE) {
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_STORE;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_store.address
                                                  : ins->value.store.address;
            const MinicCoreValueId value = atomic ? ins->value.atomic_store.stored_value
                                                   : ins->value.store.stored_value;
            size_t width = 0U;
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_store_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return minic_multi_direct_machine_reject(fn, "aarch64",
                    name, reject_stage, reject_block, reject_ins, __LINE__);
            if (fprintf(out, "  ldr x10, [sp, #%zu]\n", (size_t)addr * 8U) < 0 ||
                fprintf(out, (pointer || width == 8U) ? "  ldr x9, [sp, #%zu]\n"
                                                : "  ldr w9, [sp, #%zu]\n",
                        (size_t)value * 8U) < 0 ||
                (pointer ? fputs("  str x9, [x10]\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if (!minic_multi_direct_result_slot_offset(
                fn, ins, true, &offset)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_RESULT_REJECT arch=aarch64 function=%s block=%zu instruction=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                              name, block_id, (unsigned int)ins_id,
                              (int)ins->kind, (unsigned int)ins->result,
                              minic_type_is_integer(ins->type) ? 1 : 0,
                              minic_type_is_pointer(ins->type) ? 1 : 0);
            return minic_multi_direct_machine_reject(
                fn, "aarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
        }
        switch (ins->kind) {
        /* Ordered IEEE comparisons: AArch64 FCMP sets C=1 on unordered.
         * 'lt' (N!=V) is WRONG for NaN. Use unsigned lo/ls conditions:
         * those are true only for ordered < or <= respectively. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *condition = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                ? "eq" : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS ? "lo" : "ls";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out,
                        "  ldr d9, [sp, #%zu]\n"
                        "  ldr d10, [sp, #%zu]\n"
                        "  fcmp d9, d10\n"
                        "  cset w9, %s\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U,
                        condition) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        /* M37: cJSON compare_double's a-b and tolerance multiplication.
         * Keep full IEEE-754 binary64 payloads in FP registers/SSA slots. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                ? "  fadd d9, d9, d10\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT
                    ? "  fsub d9, d9, d10\n"
                    : "  fmul d9, d9, d10\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out, "  ldr d9, [sp, #%zu]\n"
                             "  ldr d10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                fprintf(out, "  str d9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out,
                        "  ldr d9, [sp, #%zu]\n"
                        "  ldr d10, [sp, #%zu]\n"
                        "  fdiv d9, d9, d10\n"
                        "  str d9, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue; /* Result is already in its full 64-bit SSA slot. */
        }
        /* Unary IEEE-754 sign flip includes -0 and all NaN payloads. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out, "  ldr d9, [sp, #%zu]\n"
                             "  fneg d9, d9\n"
                             "  str d9, [sp, #%zu]\n",
                        (size_t)operand * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT: {
            if (minic_type_is_long_double(ins->type)) {
                size_t dst, half;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (half = 0U; half < 2U; ++half) {
                    const uint64_t bits = half == 0U
                        ? ins->value.floating128_bits.low
                        : ins->value.floating128_bits.high;
                    unsigned int shift;
                    if (fprintf(out, "  movz x9, #%" PRIu64 "\n",
                                bits & UINT64_C(0xffff)) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                    for (shift = 16U; shift <= 48U; shift += 16U)
                        if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                            fprintf(out, "  movk x9, #%" PRIu64
                                         ", lsl #%u\n",
                                    (bits >> shift) & UINT64_C(0xffff),
                                    shift) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    if (fprintf(out, "  str x9, [sp, #%zu]\n",
                                dst + half * 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            const uint64_t bits = ins->value.floating_bits;
            unsigned int shift;
            if ((!minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type)) ||
                fprintf(out, "  movz x9, #%" PRIu64 "\n",
                        bits & UINT64_C(0xffff)) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            for (shift = 16U; shift <= 48U; shift += 16U) {
                if (((bits >> shift) & UINT64_C(0xffff)) != 0U &&
                    fprintf(out, "  movk x9, #%" PRIu64 ", lsl #%u\n",
                            (bits >> shift) & UINT64_C(0xffff), shift) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            break;
        }
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
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
            /* Complete 32/64-bit signed+unsigned GNU checked arithmetic.
             * Result bytes are written through Core's pointer; x9 carries
             * the boolean status to the normal SSA destination afterward. */
            const MinicCoreValueId lhs = ins->value.integer_overflow.left;
            const MinicCoreValueId rhs = ins->value.integer_overflow.right;
            const MinicCoreValueId address = ins->value.integer_overflow.result_address;
            MinicType value_type;
            size_t width;
            bool is_signed;
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                address >= fn->value_count ||
                !minic_type_pointee(fn->values[address].type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                minic_type_is_bool_integer(value_type) ||
                minic_type_is_const(value_type) ||
                minic_type_is_volatile(value_type) ||
                !minic_type_equal(value_type, fn->values[lhs].type) ||
                !minic_type_equal(value_type, fn->values[rhs].type) ||
                !minic_lp64_scalar_integer_width(program, target, value_type,
                                      &width, &is_signed) ||
                (width != 4U && width != 8U))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            const bool wide = width == 8U;
            const int op = ins->value.integer_overflow.operator_kind;
            if (fprintf(out, wide
                      ? "  ldr x9, [sp, #%zu]\n  ldr x10, [sp, #%zu]\n"
                      : "  ldr w9, [sp, #%zu]\n  ldr w10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fprintf(out, "  ldr x11, [sp, #%zu]\n",
                        (size_t)address * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
                op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
                const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
                const char *instruction = wide
                    ? (sub ? "  subs x9, x9, x10\n"
                           : "  adds x9, x9, x10\n")
                    : (sub ? "  subs w9, w9, w10\n"
                           : "  adds w9, w9, w10\n");
                const char *flag = is_signed ? "vs" : sub ? "cc" : "cs";
                if (fputs(instruction, out) == EOF ||
                    fprintf(out, "  cset w10, %s\n", flag) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (wide) {
                    if (fputs(is_signed ? "  smulh x12, x9, x10\n"
                                        : "  umulh x12, x9, x10\n", out) == EOF ||
                        fputs("  mul x9, x9, x10\n", out) == EOF ||
                        fputs(is_signed
                             ? "  asr x10, x9, #63\n  cmp x12, x10\n"
                             : "  cmp x12, #0\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else if (fputs(is_signed
                            ? "  smull x12, w9, w10\n"
                              "  sxtw x10, w12\n  cmp x12, x10\n"
                            : "  umull x12, w9, w10\n"
                              "  lsr x10, x12, #32\n  cmp x10, #0\n",
                            out) == EOF ||
                           fputs("  mov w9, w12\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                if (fputs("  cset w10, ne\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            } else return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fputs(wide ? "  str x9, [x11]\n"
                          : "  str w9, [x11]\n", out) == EOF ||
                fputs("  mov w9, w10\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CLZ:
        case MINIC_CORE_INSTRUCTION_INTEGER_CTZ: {
            const MinicCoreValueId operand = ins->value.operand;
            unsigned int width;
            if (operand >= fn->value_count ||
                !minic_type_is_integer(ins->type) ||
                !minic_type_is_unsigned_integer(fn->values[operand].type) ||
                !minic_target_info_integer_width(target, program,
                          fn->values[operand].type, &width) ||
                (width != 8U && width != 16U &&
                 width != 32U && width != 64U) ||
                fprintf(out, width == 64U
                           ? "  ldr x9, [sp, #%zu]\n"
                           : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                (width != 64U && width < 32U &&
                 fprintf(out, "  and w9, w9, #%u\n",
                         (1U << width) - 1U) < 0))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CTZ &&
                fputs(width == 64U ? "  rbit x9, x9\n"
                                   : "  rbit w9, w9\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fputs(width == 64U ? "  clz x9, x9\n"
                                   : "  clz w9, w9\n", out) == EOF ||
                (width < 32U &&
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
                 fprintf(out, "  sub w9, w9, #%u\n", 32U - width) < 0))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_LOAD:
        case MINIC_CORE_INSTRUCTION_ATOMIC_LOAD: {
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_int128_integer(ins->type)) {
                MinicCoreValueId addr;
                size_t dst;
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n"
                                 "  ldr x9, [x10]\n"
                                 "  ldr x11, [x10, #8]\n"
                                 "  str x9, [sp, #%zu]\n"
                                 "  str x11, [sp, #%zu]\n",
                            (size_t)addr * 8U, dst, dst + 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                continue;
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_long_double(ins->type)) {
                MinicCoreValueId addr;
                size_t dst;
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst) ||
                fprintf(out, "  ldr x10, [sp, #%zu]\n"
                                 "  ldr q9, [x10]\n"
                                 "  str q9, [sp, #%zu]\n",
                            (size_t)addr * 8U, dst) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            const bool atomic = ins->kind == MINIC_CORE_INSTRUCTION_ATOMIC_LOAD;
            const MinicCoreValueId addr = atomic ? ins->value.atomic_load.address
                                                  : ins->value.load.address;
            size_t width = 0U;
            bool sign = false, pointer = false;
            if (!minic_multi_direct_scalar_load_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (
                fprintf(out, "  ldr x10, [sp, #%zu]\n", (size_t)addr * 8U) < 0 ||
                (pointer ? fputs("  ldr x9, [x10]\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_CALL:
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
            const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
            const size_t begin = indirect ? ins->value.indirect_call.argument_begin
                                          : ins->value.call.argument_begin;
            const size_t count = indirect ? ins->value.indirect_call.argument_count
                                          : ins->value.call.argument_count;
            size_t j;
            size_t stack_bytes;
            MinicLP64ArgumentPlan call_plan;
            /* Resolve ABI locations once before allocating outgoing stack
             * space; the same plan drives each argument's source cursor. */
            if (!minic_lp64_plan_direct_call(
                    program, target, fn, ins,
                    MINIC_LP64_ABI_AAPCS64,
                    minic_aarch64_indirect_record,
                    &call_plan))
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            stack_bytes = call_plan.stack_bytes;
            if (stack_bytes != 0U &&
                fprintf(out, "  sub sp, sp, #%zu\n", stack_bytes) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicLP64ArgumentLocation *location = &call_plan.arguments[j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (!minic_lp64_frame_plan_object(frame_plan, object_id, &object_offset))
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                              reject_stage, reject_block, reject_ins, __LINE__);
                    if (!minic_multi_gp_record(program, target,
                            fn->objects[object_id].type, &record_size, &slots)) {
                        const size_t src = object_offset + stack_bytes;
                        if (!minic_aarch64_indirect_record(program, target,
                                fn->objects[object_id].type, &record_size) ||
                            fprintf(out, "  movz x9, #%zu\n",
                                    src & 65535U) < 0 ||
                            (src > 65535U &&
                             fprintf(out, "  movk x9, #%zu, lsl #16\n",
                                     src >> 16U) < 0) ||
                            fputs("  add x9, sp, x9\n", out) == EOF ||
                            (location->gp_registers != 0U
                             ? fprintf(out, "  mov x%zu, x9\n", location->gp_first) < 0
                             : fprintf(out, "  str x9, [sp, #%zu]\n",
                                       location->stack_first * 8U) < 0))
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const bool in_reg = location->gp_registers == slots;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, object_offset + stack_bytes + chunk * 8U,
                                payload, in_reg, in_reg ? location->gp_first + chunk : 0U,
                                in_reg ? 0U : location->stack_first + chunk))
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                  reject_stage, reject_block, reject_ins, __LINE__);
                    }
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U + stack_bytes;
                if (minic_type_is_long_double(type)) {
                    size_t src;
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, arg->value.value_id, &src))
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                    src += stack_bytes;
                    if (location->fp_registers != 0U) {
                        if (fprintf(out, "  ldr q%zu, [sp, #%zu]\n",
                                    location->fp_first, src) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    } else {
                        if (fprintf(out, "  ldr q9, [sp, #%zu]\n"
                                         "  str q9, [sp, #%zu]\n",
                                    src, location->stack_first * 8U) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    }
                } else if (minic_type_is_double(type) || minic_type_is_float(type)) {
                    const bool is_float = minic_type_is_float(type);
                    if (location->fp_registers != 0U) {
                        if (fprintf(out, is_float ? "  ldr s%zu, [sp, #%zu]\n"
                                                  : "  ldr d%zu, [sp, #%zu]\n",
                                    location->fp_first, slot) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    } else if (fprintf(out, is_float
                                       ? "  ldr s9, [sp, #%zu]\n"
                                         "  str s9, [sp, #%zu]\n"
                                       : "  ldr d9, [sp, #%zu]\n"
                                         "  str d9, [sp, #%zu]\n",
                                       slot, location->stack_first * 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                } else {
                    const bool wide = minic_lp64_register64_type(program, target, type);
                    if (location->gp_registers != 0U) {
                        if (fprintf(out, wide ? "  ldr x%zu, [sp, #%zu]\n"
                                              : "  ldr w%zu, [sp, #%zu]\n",
                                    location->gp_first, slot) < 0)
                            return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    } else if (fprintf(out,
                                       wide ? "  ldr x9, [sp, #%zu]\n"
                                              "  str x9, [sp, #%zu]\n"
                                            : "  ldr w9, [sp, #%zu]\n"
                                              "  str w9, [sp, #%zu]\n",
                                       slot, location->stack_first * 8U) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                }
            }
            if (indirect) {
                if (fprintf(out, "  ldr x16, [sp, #%zu]\n"
                                 "  blr x16\n",
                            (size_t)ins->value.indirect_call.callee * 8U +
                            stack_bytes) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                if (fprintf(out, "  bl %s\n", callee->name) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
            if (stack_bytes != 0U &&
                fprintf(out, "  add sp, sp, #%zu\n", stack_bytes) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (minic_type_is_void(ins->type)) continue;
            if (minic_type_is_record(ins->type)) {
                const MinicCoreObjectId object_id =
                    ins->value.call.result_object;
                size_t bytes, slots, dst, chunk;
                if (indirect || object_id >= fn->object_count ||
                    !minic_multi_gp_record(program, target, ins->type,
                                           &bytes, &slots) ||
                    bytes == 0U || bytes > 16U || slots > 2U ||
                    !minic_lp64_frame_plan_object(frame_plan, object_id, &dst))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!minic_aarch64_emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U))
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    fprintf(out, "  str q0, [sp, #%zu]\n", dst) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            if (fputs(minic_type_is_double(ins->type)
                          ? "  fmov x9, d0\n"
                          : minic_type_is_float(ins->type)
                              ? "  fmov w9, s0\n"
                          : minic_lp64_register64_type(program, target, ins->type)
                              ? "  mov x9, x0\n"
                              : "  mov w9, w0\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) ||
                    (width != 8U && !emit_integer_normalize(out, width, sign)))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                               reject_block, reject_ins, __LINE__);
            }
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
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            const size_t ordinal = (size_t)ins->value.parameter_index;
            const MinicLP64ArgumentLocation *location =
                ordinal < incoming_plan->argument_count
                    ? &incoming_plan->arguments[ordinal] : NULL;
            if (location == NULL || !minic_type_equal(ins->type,
                                                    fn->parameter_types[ordinal]) ||
                location->kind == MINIC_LP64_ARGUMENT_RECORD ||
                location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                if (location->fp_registers != 0U) {
                    if (fprintf(out, "  str q%zu, [sp, #%zu]\n",
                                location->fp_first, dst) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                } else {
                    if (fprintf(out, "  ldr q9, [sp, #%zu]\n"
                                     "  str q9, [sp, #%zu]\n",
                                frame + location->stack_first * 8U, dst) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if (minic_type_is_double(ins->type) ||
                minic_type_is_float(ins->type)) {
                const bool is_float = minic_type_is_float(ins->type);
                if (location->fp_registers != 0U) {
                    if (fprintf(out, is_float ? "  str s%zu, [sp, #%zu]\n"
                                              : "  str d%zu, [sp, #%zu]\n",
                                location->fp_first, offset) < 0)
                        return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                } else if (fprintf(out, is_float
                                     ? "  ldr s9, [sp, #%zu]\n"
                                       "  str s9, [sp, #%zu]\n"
                                     : "  ldr d9, [sp, #%zu]\n"
                                       "  str d9, [sp, #%zu]\n",
                                   frame + location->stack_first * 8U, offset) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                continue;
            }
            if (location->gp_registers != 0U) {
                if (fprintf(out, minic_lp64_register64_type(program, target, ins->type)
                                 ? "  str x%zu, [sp, #%zu]\n"
                                 : "  str w%zu, [sp, #%zu]\n",
                            location->gp_first, offset) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            } else if (fprintf(out, minic_lp64_register64_type(program, target, ins->type)
                               ? "  ldr x9, [sp, #%zu]\n"
                                 "  str x9, [sp, #%zu]\n"
                               : "  ldr w9, [sp, #%zu]\n"
                                 "  str w9, [sp, #%zu]\n",
                               frame + location->stack_first * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT:
        case MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            if (ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT &&
                minic_type_is_int128_integer(ins->type)) {
                const MinicCoreValueId rhs = ins->value.binary.right;
                size_t src_offset, dst_offset, rw;
                bool rs;
                const bool sign = minic_type_is_signed_integer(ins->type);
                if (lhs >= fn->value_count || rhs >= fn->value_count ||
                    !minic_type_is_int128_integer(fn->values[lhs].type) ||
                    !minic_lp64_scalar_integer_width(program, target,
                        fn->values[rhs].type, &rw, &rs) ||
                    !minic_lp64_frame_plan_wide_slot(frame_plan, lhs, &src_offset) ||
                    !minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst_offset) ||
                    fprintf(out, rw == 8U ?
                            "  ldr x10, [sp, #%zu]\n" :
                            "  ldr w10, [sp, #%zu]\n", (size_t)rhs * 8U) < 0 ||
                    fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x11, [sp, #%zu]\n",
                            src_offset, src_offset + 8U) < 0 ||
                    fputs("  cbz x10, 2f\n"
                          "  cmp x10, #64\n"
                          "  b.hs 1f\n"
                          "  neg x13, x10\n"
                          "  lsl x13, x11, x13\n"
                          "  lsr x9, x9, x10\n"
                          "  orr x9, x9, x13\n", out) == EOF ||
                    fputs(sign ? "  asr x11, x11, x10\n" :
                                 "  lsr x11, x11, x10\n", out) == EOF ||
                    fputs("  b 2f\n"
                          "1:\n"
                          "  sub x10, x10, #64\n", out) == EOF ||
                    fputs(sign ? "  asr x9, x11, x10\n"
                                 "  asr x11, x11, #63\n" :
                                 "  lsr x9, x11, x10\n"
                                 "  mov x11, xzr\n", out) == EOF ||
                    fprintf(out, "2:\n"
                                 "  str x9, [sp, #%zu]\n"
                                 "  str x11, [sp, #%zu]\n",
                            dst_offset, dst_offset + 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                (void)rs;
                continue;
            }
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *op;
            if (!minic_multi_direct_integer_shift_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                     ? (wide ? "  lsl x9, x9, x10\n"
                             : "  lsl w9, w9, w10\n")
                     : (ls ? (wide ? "  asr x9, x9, x10\n"
                                   : "  asr w9, w9, w10\n")
                           : (wide ? "  lsr x9, x9, x10\n"
                                   : "  lsr w9, w9, w10\n"));
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                /* Narrow signed shifts need sign-extension BEFORE the
                 * arithmetic shift; normalizing only the result is late. */
                (lw < 4U && !emit_integer_normalize(out, lw, ls)) ||
                fprintf(out, rw == 8U ? "  ldr x10, [sp, #%zu]\n"
                                     : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            (void)rs;
            break;
        }
        /* Original cJSON uses ~ on signed/unsigned scalar values.
         * Preserve C-width truncation for narrow integers and the full
         * 64-bit complement for LP64 values. */
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                fprintf(out, width == 8U ? "  ldr x9, [sp, #%zu]\n"
                                         : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                fputs(width == 8U ? "  mvn x9, x9\n" : "  mvn w9, w9\n",
                      out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                fprintf(out, width == 8U ? "  ldr x9, [sp, #%zu]\n"
                                         : "  ldr w9, [sp, #%zu]\n",
                        (size_t)operand * 8U) < 0 ||
                fputs(width == 8U ? "  neg x9, x9\n" : "  neg w9, w9\n",
                      out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_ADD:
        case MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY: {
            MinicCoreValueId lhs = ins->value.binary.left;
            MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *opcode;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            wide = lw == 8U;
            /* M36: target-native LP64 multiplication, including cJSON's
             * size_t newsize = needed * 2; low 64 bits follow C unsigned
             * arithmetic; signed overflow is undefined at the source. */
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? (wide ? "  add x9, x9, x10\n" : "  add w9, w9, w10\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? (wide ? "  sub x9, x9, x10\n" : "  sub w9, w9, w10\n")
                    : (wide ? "  mul x9, x9, x10\n" : "  mul w9, w9, w10\n");
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR:
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs;
            bool wide;
            const char *opcode;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            wide = lw == 8U;
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? (wide ? "  and x9, x9, x10\n" : "  and w9, w9, w10\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? (wide ? "  eor x9, x9, x10\n" : "  eor w9, w9, w10\n")
                    : (wide ? "  orr x9, x9, x10\n" : "  orr w9, w9, w10\n");
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(opcode, out) == EOF ||
                (lw != 8U && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE:
        case MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs, wide;
            const char *op;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            wide = lw == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                ? (wide ? (ls ? "  sdiv x9, x9, x10\n"
                               : "  udiv x9, x9, x10\n")
                        : (ls ? "  sdiv w9, w9, w10\n"
                               : "  udiv w9, w9, w10\n"))
                : (wide ? (ls ? "  sdiv x11, x9, x10\n"
                               "  msub x9, x11, x10, x9\n"
                             : "  udiv x11, x9, x10\n"
                               "  msub x9, x11, x10, x9\n")
                        : (ls ? "  sdiv w11, w9, w10\n"
                               "  msub w9, w11, w10, w9\n"
                             : "  udiv w11, w9, w10\n"
                               "  msub w9, w11, w10, w9\n"));
            if (fputs(op, out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        /* M33: cJSON_SetValuestring uses pointer ordering for overlap
         * checks; the Core verifier has confirmed compatible pointer types.
         * Compare the entire 64-bit address unsigned (not low 32 bits). */
        case MINIC_CORE_INSTRUCTION_POINTER_LESS: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_pointer_less_shape(fn, ins) ||
                fprintf(out, "  ldr x9, [sp, #%zu]\n"
                             "  ldr x10, [sp, #%zu]\n"
                             "  cmp x9, x10\n"
                             "  cset w9, lo\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_LESS:
        case MINIC_CORE_INSTRUCTION_SCALAR_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            size_t lw, rw;
            bool ls, rs, wide;
            if (ins->kind == MINIC_CORE_INSTRUCTION_SCALAR_EQUAL &&
                lhs < fn->value_count && rhs < fn->value_count &&
                minic_type_is_pointer(fn->values[lhs].type) &&
                minic_type_is_pointer(fn->values[rhs].type)) {
                if (fprintf(out, "  ldr x9, [sp, #%zu]\n"
                                 "  ldr x10, [sp, #%zu]\n"
                                 "  cmp x9, x10\n  cset w9, eq\n",
                            (size_t)lhs * 8U, (size_t)rhs * 8U) < 0)
                    return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (!minic_multi_direct_integer_compare_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)lhs * 8U) < 0 ||
                fprintf(out, wide ? "  ldr x10, [sp, #%zu]\n"
                                  : "  ldr w10, [sp, #%zu]\n",
                        (size_t)rhs * 8U) < 0 ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (wide
                              ? (ls ? "  cmp x9, x10\n  cset w9, lt\n"
                                    : "  cmp x9, x10\n  cset w9, lo\n")
                              : (ls ? "  cmp w9, w10\n  cset w9, lt\n"
                                    : "  cmp w9, w10\n  cset w9, lo\n"))
                          : (wide ? "  cmp x9, x10\n  cset w9, eq\n"
                                  : "  cmp w9, w10\n  cset w9, eq\n"), out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO: {
            const MinicCoreValueId v = ins->value.operand;
            size_t width = 0U;
            bool sign = false;
            bool pointer, wide;
            if (!minic_multi_direct_scalar_zero_shape(
                    program, target, fn, ins, &width, &sign, &pointer))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = pointer || width == 8U;
            if (fprintf(out, wide ? "  ldr x9, [sp, #%zu]\n"
                                  : "  ldr w9, [sp, #%zu]\n",
                        (size_t)v * 8U) < 0 ||
                fputs(wide ? "  cmp x9, #0\n  cset w9, eq\n"
                           : "  cmp w9, #0\n  cset w9, eq\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            (void)sign;
            break;
        }
        /* M28: cJSON_CreateNumber converts INT_MIN to binary64, and after
         * saturation converts an in-range double to signed int.
         * Keep wider and unsigned cases fail-closed until ABI-tested. */
        /* M31: cJSON_CreateFloatArray's float element must be interpreted
         * numerically, not zero-extended as its 32-bit IEEE bit pattern. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
            const MinicCoreValueId source = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out, "  ldr d9, [sp, #%zu]\n"
                             "  fcvt s9, d9\n"
                             "  str s9, [sp, #%zu]\n",
                        (size_t)source * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_ADD:
        case MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY:
        case MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD
                ? "  fadd s9, s9, s10\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                    ? "  fsub s9, s9, s10\n"
                    : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                        ? "  fmul s9, s9, s10\n"
                        : "  fdiv s9, s9, s10\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out, "  ldr s9, [sp, #%zu]\n"
                             "  ldr s10, [sp, #%zu]\n",
                        (size_t)lhs * 8U, (size_t)rhs * 8U) < 0 ||
                fputs(op, out) == EOF ||
                fprintf(out, "  str s9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                fprintf(out,
                        "  ldr s9, [sp, #%zu]\n"
                        "  fcvt d9, s9\n"
                        "  str d9, [sp, #%zu]\n",
                        (size_t)src * 8U, offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            /* Numeric C conversion: AArch64 has signed AND unsigned 32/64
             * integer-to-double instructions. Narrow source values retain
             * their Core signedness through explicit normalization. */
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign) ||
                fprintf(out, width == 8U
                             ? "  ldr x9, [sp, #%zu]\n"
                             : "  ldr w9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0 ||
                (width < 4U && !emit_integer_normalize(out, width, sign)) ||
                fputs(width == 8U
                          ? (sign ? "  scvtf d9, x9\n"
                                  : "  ucvtf d9, x9\n")
                          : (sign ? "  scvtf d9, w9\n"
                                  : "  ucvtf d9, w9\n"), out) == EOF ||
                fprintf(out, "  str d9, [sp, #%zu]\n", offset) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            const char *conversion;
            /* Lua's lua_Integer is signed LP64; C double-to-integer
             * conversion truncates toward zero. Keep unsigned64 fail-closed
             * until its full-range ABI oracle is introduced. */
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            /* A64 natively supports unsigned 64-bit binary64 conversion.
             * C-defined finite inputs truncate toward zero; NaN/overflow
             * inputs remain outside the C conversion contract. */
            conversion = width == 8U
                ? (sign ? "  fcvtzs x9, d9\n"
                        : "  fcvtzu x9, d9\n")
                : sign ? "  fcvtzs w9, d9\n"
                       : "  fcvtzu w9, d9\n";
            if (fprintf(out, "  ldr d9, [sp, #%zu]\n",
                        (size_t)src * 8U) < 0 ||
                fputs(conversion, out) == EOF ||
                (width < 4U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
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
        if (fprintf(out, (minic_lp64_register64_type(program, target, ins->type) ||
                          minic_type_is_double(ins->type))
                             ? "  str x9, [sp, #%zu]\n"
                             : "  str w9, [sp, #%zu]\n", offset) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
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
