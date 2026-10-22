#include "target/loongarch64/block_emit.h"
#include "target/loongarch64/call_emit.h"
#include "target/loongarch64/integer_emit.h"
#include "target/loongarch64/fp_emit.h"
#include "target/common/direct_instruction_semantics.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/frame_emit.h"
#include "target/data_layout.h"
#include "target/common/direct_program_emit.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

/* The instruction-owned aggregate ingress and CFG terminator cases are
 * independent of the function-level preflight and frame lifetime. Machine
 * register syntax remains strictly owned by this ISA. */

bool minic_loongarch64_emit_lp64_record_chunk_parameter(FILE *out, size_t offset,
                                              size_t bytes, bool in_reg,
                                              size_t gp, size_t incoming_offset,
                                              bool dynamic_frame) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  li.d $t2, %zu\n"
                     "  add.d $t2, %s, $t2\n", offset,
                dynamic_frame ? "$s0" : "$sp") < 0 ||
        (in_reg
         ? fprintf(out, "  or $t0, $a%zu, $zero\n", gp) < 0
         : !minic_loongarch64_emit_frame_mem(out, "ld.d", "$t0",
                           incoming_offset, dynamic_frame)))
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  st.b $t0, $t2, %zu\n", b) < 0 ||
            (b + 1U < bytes && fputs("  srli.d $t0, $t0, 8\n", out) == EOF))
            return false;
    return true;
}

bool minic_loongarch64_emit_record_copy(FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins, bool dynamic_frame) {
const MinicCoreValueId dst = ins->value.record_copy.destination_address;
            const MinicCoreValueId src = ins->value.record_copy.source_address;
            size_t bytes;
            if (!minic_multi_direct_record_copy_extent(program, target, fn, ins, &bytes))
                return false;
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)dst * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t2", (size_t)src * 8U))
                return false;
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ld.bu $t0, $t2, 0\n"
                          "  st.b $t0, $t1, 0\n"
                          "  addi.d $t2, $t2, 1\n"
                          "  addi.d $t1, $t1, 1\n", out) == EOF)
                    return false;
            return true;
}

bool minic_loongarch64_emit_parameter_object(FILE *out,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64ArgumentPlan *incoming_plan,
    const MinicLP64FramePlan *frame_plan, size_t frame, bool dynamic_frame) {
const MinicLP64ArgumentLocation *location;
            size_t size, slots, object_offset, k, chunk;
            if (!minic_multi_direct_parameter_object_location(
                    fn, ins, incoming_plan, frame_plan, &location, &object_offset))
                return false;
            size = location->size_bytes;
            slots = location->kind == MINIC_LP64_ARGUMENT_RECORD
                ? location->chunks : 0U;
            if (location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD) {
                if ((location->gp_registers != 0U
                     ? fprintf(out, "  move $t0, $a%zu\n", location->gp_first) < 0
                     : !emit_sp_mem(out, "ld.d", "$t0",
                                    frame + location->stack_first * 8U)) ||
                    fprintf(out, "  li.d $t1, %zu\n"
                                 "  add.d $t1, %s, $t1\n",
                            object_offset, dynamic_frame ? "$s0" : "$sp") < 0)
                    return false;
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ld.bu $t2, $t0, 0\n"
                              "  st.b $t2, $t1, 0\n"
                              "  addi.d $t0, $t0, 1\n"
                              "  addi.d $t1, $t1, 1\n", out) == EOF)
                        return false;
                }
                return true;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const bool in_reg = chunk < location->gp_registers;
                const size_t stack_slot = in_reg ? 0U :
                    location->stack_first + chunk - location->gp_registers;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!minic_loongarch64_emit_lp64_record_chunk_parameter(out,
                        object_offset + chunk * 8U, payload,
                        in_reg, in_reg ? location->gp_first + chunk : 0U,
                        frame + stack_slot * 8U,
                        dynamic_frame))
                    return false;
            }
            return true;
}

bool minic_loongarch64_emit_terminator(FILE *out,
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
                    !emit_sp_mem(out, "ld.d", "$a0", src) ||
                    !emit_sp_mem(out, "ld.d", "$a1", src + 8U))
                    return false;
            }
            if ((!minic_type_is_void(fn->return_type) &&
                 !minic_type_is_long_double(fn->return_type) &&
                 !((minic_type_is_double(fn->return_type) ||
                  minic_type_is_float(fn->return_type))
                       ? emit_sp_mem(out,
                                     minic_type_is_float(fn->return_type)
                                         ? "fld.s" : "fld.d", "$fa0",
                                     (size_t)block->terminator.return_value * 8U)
                       : emit_sp_mem(out, minic_lp64_register64_type(program, target, fn->return_type)
                                          ? "ld.d" : "ld.w", "$a0",
                                     (size_t)block->terminator.return_value * 8U))) ||
                (has_direct_calls && !emit_sp_mem(out, "ld.d", "$ra", save_offset)) ||
                (dynamic_frame &&
                 (fputs("  move $sp, $s0\n", out) == EOF ||
                  !minic_loongarch64_emit_frame_mem(out, "ld.d", "$s0",
                                  save_offset + (has_direct_calls ? 8U : 0U),
                                  false))) ||
                (!minic_loongarch64_emit_sp_adjust(out, frame, true)) ||
                fputs("  ret\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (!emit_sp_mem(out, "ld.d", "$t0",
                             (size_t)block->terminator.indirect_target * 8U) ||
                fputs("  jirl $zero, $t0, 0\n", out) == EOF)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  break 0\n", out) == EOF) return false;
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return false;
            break;
        case MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH: {
            const MinicCoreValueId cond = block->terminator.conditional.condition;
            const bool wide = minic_lp64_register64_type(program, target, fn->values[cond].type);
            /* The short LoongArch conditional jump must remain nearby. */
            if (fn->block_count > 256U) {
                if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                                 (size_t)cond * 8U) ||
                    fprintf(out,
                            "  beqz $t0, .Lmini_loong_f%zu_false_%zu\n"
                            "  b .Lmini_loong_f%zu_b%u\n"
                            ".Lmini_loong_f%zu_false_%zu:\n"
                            "  b .Lmini_loong_f%zu_b%u\n",
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_true,
                            id, block_id,
                            id, (unsigned int)block->terminator.conditional.when_false) < 0)
                    return false;
                break;
            }
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)cond * 8U) ||
                fprintf(out,
                        "  bnez $t0, .Lmini_loong_f%zu_b%u\n"
                        "  b .Lmini_loong_f%zu_b%u\n",
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
        op = width == 1U ? "  st.b $t0, $t1, 0\n" :
             width == 2U ? "  st.h $t0, $t1, 0\n" :
             width == 4U ? "  st.w $t0, $t1, 0\n" :
             width == 8U ? "  st.d $t0, $t1, 0\n" : NULL;
    } else {
        op = width == 1U ? (sign ? "  ld.b $t0, $t1, 0\n" :
                                     "  ld.bu $t0, $t1, 0\n") :
             width == 2U ? (sign ? "  ld.h $t0, $t1, 0\n" :
                                     "  ld.hu $t0, $t1, 0\n") :
             width == 4U ? "  ld.w $t0, $t1, 0\n" :
              width == 8U ? "  ld.d $t0, $t1, 0\n" : NULL;
    }
    return op != NULL && fputs(op, out) != EOF;
}

static bool emit_integer_normalize(FILE *out, size_t width, bool sign) {
    const char *op = width == 1U
        ? (sign ? "  slli.w $t0, $t0, 24\n  srai.w $t0, $t0, 24\n" :
                  "  andi $t0, $t0, 255\n")
        : width == 2U
            ? (sign ? "  slli.w $t0, $t0, 16\n  srai.w $t0, $t0, 16\n" :
                      "  bstrpick.w $t0, $t0, 15, 0\n")
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





/* M7: retain x86's established DataLayout-owned object sizing instead
 * of guessing eight bytes per Core object.  The new-ISA stack slots
 * are SP-positive; naturally aligned objects are laid out after SSA
 * value slots.  Keep bounded <= 8 KiB frames until large-offset ABI
 * access has been independently checked on both targets. */




/* Preserve binary128 SSA values in distinct sixteen-byte aligned slots,
 * rather than overwriting adjacent eight-byte scalar values. */


/* lp64d load/store immediates are signed 12-bit. Keep the original
 * direct encoding for small frames; for real KeyExpansion-sized frames,
 * materialize the full SP-relative address in reserved $t3.  No other
 * emitter instruction may treat $t3 as a live Core value. */




/* Dynamic alloca changes SP. Anchor every fixed Core SSA/object slot
 * to callee-saved s0 ONLY for functions containing that instruction.
 * Raw SP remains the ABI outgoing stack-argument base. */


bool minic_loongarch64_emit_core_blocks(FILE *out, const MinicDirectLP64BlockContext *ctx) {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
    const MinicCoreFunction *fn;
    const MinicLP64FramePlan *frame_plan;
    const MinicLP64ArgumentPlan *incoming_plan;
    const char *name, *reject_stage;
    const MinicCoreBlock *block;
    size_t id, frame, save_offset, va_count, va_gp, i;
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
    has_direct_calls = ctx->has_direct_calls;
    dynamic_frame = ctx->dynamic_frame;
    reject_stage = "machine_emit";
    for (size_t block_id = 0U; block_id < fn->block_count; ++block_id) {
        reject_block = block_id;
        reject_ins = SIZE_MAX;
        reject_stage = "machine_emit";
        block = &fn->blocks[block_id];
        if (fprintf(out, ".Lmini_loong_f%zu_b%zu:\n", id, block_id) < 0 ||
            (block->source_label_id != SIZE_MAX &&
             !fn->suppress_source_label_aliases &&
             fprintf(out, ".Luser_%zu:\n", block->source_label_id) < 0))
            return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        for (i = 0U; i < block->instruction_count; ++i) {
        MinicCoreInstructionId ins_id = block->instructions[i];
        const MinicCoreInstruction *ins;
        size_t offset;
        if (ins_id >= fn->instruction_count) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            continue;
        }
        /* M23: copy the *exact* target-sized aggregate bytes between Core
         * addresses, preserving padding and nested hooks (no re-layout).
         * An effect-only record copy has no SSA result slot. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_RECORD_COPY) {
            if (!minic_loongarch64_emit_record_copy(out, program, target, fn, ins, dynamic_frame))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
            if (!minic_loongarch64_emit_parameter_object(out, fn, ins,
                    incoming_plan, frame_plan, frame, dynamic_frame))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        /* Store both 64-bit limbs, with no hidden 128-bit truncation. */
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_int128_integer(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            size_t src;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t0", src) ||
                fputs("  st.d $t0, $t1, 0\n", out) == EOF ||
                !emit_sp_mem(out, "ld.d", "$t0", src + 8U) ||
                fputs("  st.d $t0, $t1, 8\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_STORE &&
            ins->value.store.stored_value < fn->value_count &&
            minic_type_is_long_double(
                fn->values[ins->value.store.stored_value].type)) {
            const MinicCoreValueId addr = ins->value.store.address;
            size_t src;
            if (!minic_multi_direct_wide_store_shape(
                    fn, ins, frame_plan, &src) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t0", src) ||
                fputs("  st.d $t0, $t1, 0\n", out) == EOF ||
                !emit_sp_mem(out, "ld.d", "$t0", src + 8U) ||
                fputs("  st.d $t0, $t1, 8\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
                return minic_multi_direct_machine_reject(fn, "loongarch64",
                    name, reject_stage, reject_block, reject_ins, __LINE__);
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, (pointer || width == 8U) ? "ld.d" : "ld.w", "$t0",
                             (size_t)value * 8U) ||
                (pointer ? fputs("  st.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if (!minic_multi_direct_result_slot_offset(
                fn, ins, false, &offset)) {
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_RESULT_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                              name, block_id, (unsigned int)ins_id,
                              (int)ins->kind, (unsigned int)ins->result,
                              minic_type_is_integer(ins->type) ? 1 : 0,
                              minic_type_is_pointer(ins->type) ? 1 : 0);
            return minic_multi_direct_machine_reject(
                fn, "loongarch64", name, reject_stage, reject_block, reject_ins, __LINE__);
        }
        /* The native FP instruction family is a single target-owned
         * emission unit; Core semantics are shared, machine opcodes are not.
         * GP-producing comparisons/conversions reuse normal writeback. */
        if (minic_multi_direct_fp_instruction_kind(ins->kind)) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_fp_instruction(
                    out, program, target, fn, ins, offset,
                    id, dynamic_frame, frame_plan,
                    emit_integer_normalize,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store) {
                if (!emit_sp_mem(out,
                        (minic_lp64_register64_type(program, target, ins->type) ||
                         minic_type_is_double(ins->type)) ? "st.d" : "st.w",
                        "$t0", offset))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage, reject_block,
                        reject_ins, __LINE__);
            }
            continue;
        }
        /* Integer and pointer ALU is a complete native instruction family.
         * Shared Core widths/shapes; ISA-dependent flags and bit arithmetic. */
        if (minic_multi_direct_integer_machine_kind(ins->kind)) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_integer_instruction(
                    out, program, target, fn, ins, frame_plan,
                    dynamic_frame, emit_integer_normalize, &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store) {
                if (!emit_sp_mem(out,
                        minic_lp64_register64_type(program, target, ins->type)
                            ? "st.d" : "st.w", "$t0", offset))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage, reject_block,
                        reject_ins, __LINE__);
            }
            continue;
        }
        /* Calls and callee ingress form a single ABI transfer family.
         * The native emitter consumes the common argument location plan. */
        if (minic_multi_direct_abi_transfer_kind(ins->kind)) {
            bool needs_gp_store = false;
            if (!minic_loongarch64_emit_abi_instruction(
                    out, ctx, ins, offset, emit_integer_normalize,
                    &needs_gp_store))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (needs_gp_store) {
                if (!emit_sp_mem(out,
                        minic_lp64_register64_type(program, target, ins->type)
                            ? "st.d" : "st.w", "$t0", offset))
                    return minic_multi_direct_machine_reject(
                        fn, "loongarch64", name, reject_stage, reject_block,
                        reject_ins, __LINE__);
            }
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
                if (fprintf(out, "  li.d $t0, %" PRId64 "\n",
                            ins->value.integer_value) < 0) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            } else {
                if ((width == 0U &&
                     (ins->value.integer_value < INT32_MIN ||
                      ins->value.integer_value > INT32_MAX)) ||
                    fprintf(out, "  li.w $t0, %" PRId64 "\n",
                            (int64_t)(int32_t)(uint32_t)
                                ins->value.integer_value) < 0 ||
                    ((width == 1U || width == 2U) &&
                     !emit_integer_normalize(out, width, sign))) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            (void)sign;
            break;
        }
        case MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS:
            if (ins->value.global_id >= fn->global_count ||
                fn->globals[ins->value.global_id].name == NULL ||
                fprintf(out, "  la.global $t0, %s\n",
                        fn->globals[ins->value.global_id].name) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        case MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS: {
            const MinicCoreFunctionSymbolId symbol_id =
                ins->value.function_symbol_id;
            const char *symbol;
            if (symbol_id >= fn->function_symbol_count ||
                !minic_type_is_pointer(ins->type)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            symbol = fn->function_symbols[symbol_id].name;
            if (symbol == NULL || symbol[0] == '\0' ||
                fprintf(out, "  la.global $t0, %s\n", symbol) < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS: {
            size_t object_offset;
            if (ins->value.object_id >= fn->object_count ||
                !minic_lp64_frame_plan_object(frame_plan, (size_t)ins->value.object_id, &object_offset) ||
                fprintf(out, "  li.d $t0, %zu\n"
                             "  add.d $t0, %s, $t0\n",
                        object_offset, dynamic_frame ? "$s0" : "$sp") < 0 ||
                !emit_sp_mem(out, "st.d", "$t0", offset)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }

        case MINIC_CORE_INSTRUCTION_FIELD_ADDRESS: {
            /* The full Core DataLayout byte offset may exceed signed imm12.
             * li.d + add.d preserves the computed record layout. */
            const MinicCoreValueId base = ins->value.field_address.base;
            const size_t bytes = ins->value.field_address.byte_offset;
            if (base >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            if (bytes != 0U) {
                if (bytes <= 2047U) {
                    if (fprintf(out, "  addi.d $t0, $t0, %zu\n", bytes) < 0)
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                } else if (fprintf(out,
                            "  li.d $t1, %zu\n"
                            "  add.d $t0, $t0, $t1\n", bytes) < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
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
                    "MINIC_M11_POINTER_DIAG arch=loongarch64 function=%s block=%zu ins=%u base=%u base_pointer=%d index=%u index_integer=%d width_known=%d index_bits=%u stride=%zu subtract=%d\n",
                    name, block_id, (unsigned int)ins_id,
                    (unsigned int)base,
                    minic_type_is_pointer(fn->values[base].type) ? 1 : 0,
                    (unsigned int)index,
                    minic_type_is_integer(fn->values[index].type) ? 1 : 0,
                    have_bits ? 1 : 0, bits, stride,
                    ins->value.pointer_offset.subtract ? 1 : 0);
            }
            if (!minic_multi_direct_pointer_offset_shape(
                    program, target, fn, ins, (size_t)-1, &iw, &signed_index) ||
                /* Full-width stride literal; keep LoongArch64-only assembly. */
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)base * 8U) ||
                !emit_sp_mem(out, iw == 1U
                                  ? (signed_index ? "ld.b" : "ld.bu")
                                  : iw == 2U
                                      ? (signed_index ? "ld.h" : "ld.hu")
                                      : iw == 8U ? "ld.d"
                                      : (signed_index ? "ld.w" : "ld.wu"),
                             "$t1", (size_t)index * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (stride != 0U) {
                if (fprintf(out, "  li.d $t2, %zu\n"
                                 "  mul.d $t1, $t1, $t2\n"
                                 "  %s $t0, $t0, $t1\n", stride,
                            ins->value.pointer_offset.subtract ? "sub.d" : "add.d") < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            if (pointer_to_integer) {
                if (!emit_sp_mem(out, "ld.d", "$t0", (size_t)source * 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (!emit_sp_mem(out, (src_pointer || src_width == 8U)
                                 ? "ld.d" : "ld.w", "$t0",
                             (size_t)source * 8U)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!src_pointer && src_width != 8U &&
                (!emit_integer_normalize(out, src_width, src_signed) ||
                 (src_signed
                    ? fputs("  slli.w $t0, $t0, 0\n", out) == EOF
                    : fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                    fputs("  ld.d $t0, $t1, 0\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    fputs("  ld.d $t0, $t1, 8\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst + 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                continue;
            }
            if (ins->kind == MINIC_CORE_INSTRUCTION_LOAD &&
                minic_type_is_long_double(ins->type)) {
                MinicCoreValueId addr;
                size_t dst;
                if (!minic_multi_direct_wide_scalar_load_location(
                        fn, ins, frame_plan, &addr, &dst) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                    fputs("  ld.d $t0, $t1, 0\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    fputs("  ld.d $t0, $t1, 8\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst + 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                (pointer ? fputs("  ld.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, false, width, sign)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS: {
            /* LoongArch LP64D va_list is a scalar byte cursor.  The
             * register bank saved at the top of this frame runs directly
             * into the caller's overflow stack slots at entry SP. */
            if (va_count != 1U || !minic_type_is_pointer(ins->type) ||
                ins->result >= fn->value_count ||
                fprintf(out, "  li.d $t0, %zu\n",
                        frame - 64U + va_gp * 8U) < 0 ||
                fprintf(out, "  add.d $t0, %s, $t0\n",
                        dynamic_frame ? "$s0" : "$sp") < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)count * 8U) ||
                fprintf(out,
                    "  addi.d $t1, $t0, 15\n"
                    "  sltu $t2, $t1, $t0\n"
                    "  beqz $t2, .Lmini_loong_alloc_ok_%zu_%u\n"
                    "  break 0\n"
                    ".Lmini_loong_alloc_ok_%zu_%u:\n"
                    "  li.d $t2, -16\n"
                    "  and $t1, $t1, $t2\n"
                    "  sub.d $sp, $sp, $t1\n"
                    "  move $t0, $sp\n",
                    id, (unsigned int)ins_id, id,
                    (unsigned int)ins_id) < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION: {
            const MinicCoreValueId src = ins->value.operand;
            size_t src_width, dst_width;
            bool src_sign, dst_sign;
            /* RV64-proven pair model, using isolated 16-byte SSA slots. */
            if (src < fn->value_count &&
                (minic_type_is_int128_integer(ins->type) ||
                 minic_type_is_int128_integer(fn->values[src].type))) {
                const bool src128 = minic_type_is_int128_integer(fn->values[src].type);
                const bool dst128 = minic_type_is_int128_integer(ins->type);
                size_t src_offset = 0U, dst_offset = 0U;
                if (src128) {
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, src, &src_offset) ||
                        !emit_sp_mem(out, "ld.d", "$t0", src_offset) ||
                        !emit_sp_mem(out, "ld.d", "$t1", src_offset + 8U))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else {
                    if (!minic_lp64_scalar_integer_width(program, target,
                            fn->values[src].type, &src_width, &src_sign) ||
                        !emit_sp_mem(out, src_width == 8U ? "ld.d" : "ld.w",
                            "$t0", (size_t)src * 8U) ||
                        (src_width < 8U &&
                         !emit_integer_normalize(out, src_width, src_sign)) ||
                        (src_width == 4U && !src_sign &&
                         fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF) ||
                        fputs(src_sign ? "  srai.d $t1, $t0, 63\n" :
                                         "  move $t1, $zero\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                }
                if (dst128) {
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst_offset) ||
                        !emit_sp_mem(out, "st.d", "$t0", dst_offset) ||
                        !emit_sp_mem(out, "st.d", "$t1", dst_offset + 8U))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                    continue;
                }
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                           &dst_width, &dst_sign) ||
                    (dst_width < 8U &&
                     !emit_integer_normalize(out, dst_width, dst_sign)))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                break;
            }
            if (src >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target,
                                      fn->values[src].type, &src_width, &src_sign) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type,
                                      &dst_width, &dst_sign) ||
                !emit_sp_mem(out, src_width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)src * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (dst_width == 8U) {
                if (src_width != 8U &&
                    (!emit_integer_normalize(out, src_width, src_sign) ||
                     (!src_sign &&
                      fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF)))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                /* LoongArch ld.w sign-extends: unsigned32 needs masking. */
            } else if (!emit_integer_normalize(out, dst_width, dst_sign))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default:
            if (getenv("MINIC_M1_TRACE") != NULL)
                (void)fprintf(stderr,
                              "MINIC_M8_OPCODE_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d\n",
                              name, block_id, (unsigned int)ins_id, (int)ins->kind);
            return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
        if (!emit_sp_mem(out, (minic_lp64_register64_type(program, target, ins->type) ||
                               minic_type_is_double(ins->type))
                              ? "st.d" : "st.w", "$t0", offset)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
        reject_stage = "terminator_emit";
        reject_ins = SIZE_MAX;
        if (!minic_loongarch64_emit_terminator(out, program, target, fn, block,
                id, block_id, frame_plan, frame, save_offset,
                has_direct_calls, dynamic_frame))
            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                reject_stage, reject_block, reject_ins, __LINE__);
    }
    return true;
}

#undef emit_sp_mem
