#include "target/loongarch64/block_emit.h"
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


/* LP64D integral aggregate bytes travel in one/two a-registers or
 * eightbyte stack slots. Use byte packing to avoid reading beyond a
 * partially filled final slot, preserving exact Core snapshot bytes. */
static bool emit_lp64_record_chunk_argument(FILE *out, size_t offset,
                                             size_t bytes, bool in_reg,
                                             size_t gp, size_t stack_slot,
                                             bool dynamic_frame) {
    size_t b;
    if (bytes == 0U || bytes > 8U || (in_reg && gp >= 8U) ||
        fprintf(out, "  li.d $t2, %zu\n"
                     "  add.d $t2, %s, $t2\n"
                     "  move $t0, $zero\n", offset,
                dynamic_frame ? "$s0" : "$sp") < 0)
        return false;
    for (b = 0U; b < bytes; ++b)
        if (fprintf(out, "  ld.bu $t1, $t2, %zu\n"
                         "  slli.d $t1, $t1, %zu\n"
                         "  or $t0, $t0, $t1\n",
                    b, b * 8U) < 0) return false;
    if (in_reg)
        return fprintf(out, "  or $a%zu, $t0, $zero\n", gp) >= 0;
    return minic_loongarch64_emit_sp_mem(out, "st.d", "$t0", stack_slot * 8U);
}

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
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                !minic_type_is_void(ins->type))
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
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t src;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, value, &src) ||
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
            const MinicCoreValueId value = ins->value.store.stored_value;
            size_t src;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                !minic_lp64_frame_plan_wide_slot(frame_plan, value, &src) ||
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
        switch (ins->kind) {
        /* LoongArch FCC ordered comparisons naturally reject unordered
         * operands: fcmp.ceq/clt/cle is false if either operand is NaN. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS:
        case MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL
                ? "fcmp.ceq.d" :
                ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                    ? "fcmp.clt.d" : "fcmp.cle.d";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.d", "$ft1", (size_t)rhs * 8U) ||
                fprintf(out, "  %s $fcc0, $ft0, $ft1\n"
                             "  movcf2gr $t0, $fcc0\n", op) < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        /* M37: source-observed cJSON binary64 subtract and multiply. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_ADD:
        case MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT:
        case MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            const char *op = ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD
                ? "  fadd.d $ft0, $ft0, $ft1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT
                    ? "  fsub.d $ft0, $ft0, $ft1\n"
                    : "  fmul.d $ft0, $ft0, $ft1\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.d", "$ft1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.d", "$ft1", (size_t)rhs * 8U) ||
                fputs("  fdiv.d $ft0, $ft0, $ft1\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)operand * 8U) ||
                fputs("  fneg.d $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT:
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                            ins->value.floating128_bits.low) < 0 ||
                    !emit_sp_mem(out, "st.d", "$t0", dst) ||
                    fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                            ins->value.floating128_bits.high) < 0 ||
                    !emit_sp_mem(out, "st.d", "$t0", dst + 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            if ((!minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type)) ||
                fprintf(out, "  li.d $t0, 0x%016" PRIx64 "\n",
                        ins->value.floating_bits) < 0) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
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
        case MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW: {
            /* Signed overflow is computed from the sign bits; unsigned
             * carry/borrow use full-width comparisons. 64-bit multiply
             * checks MULH high against the low-half sign extension. */
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
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            const int op = ins->value.integer_overflow.operator_kind;
            const bool wide = width == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                !emit_sp_mem(out, "ld.d",
                             "$t2", (size_t)address * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (!wide && !is_signed &&
                fputs("  bstrpick.d $t0, $t0, 31, 0\n"
                      "  bstrpick.d $t1, $t1, 31, 0\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
                op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT) {
                const bool sub = op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT;
                if (fputs("  or $t3, $t0, $zero\n", out) == EOF ||
                    fputs(wide
                          ? (sub ? "  sub.d $t0, $t0, $t1\n"
                                 : "  add.d $t0, $t0, $t1\n")
                          : (!is_signed && !sub
                              ? "  add.d $t0, $t0, $t1\n"
                              : sub ? "  sub.w $t0, $t0, $t1\n"
                                    : "  add.w $t0, $t0, $t1\n"), out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                if (is_signed) {
                    if (fputs(sub
                          ? "  xor $t4, $t3, $t1\n"
                            "  xor $t3, $t3, $t0\n"
                          : "  xor $t4, $t1, $t0\n"
                            "  xor $t3, $t3, $t0\n", out) == EOF ||
                        fputs("  and $t4, $t3, $t4\n", out) == EOF ||
                        (!wide && fputs("  slli.w $t4, $t4, 0\n", out) == EOF) ||
                        fputs("  slt $t4, $t4, $zero\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else if (fputs(sub ? "  sltu $t4, $t3, $t1\n"
                                     : wide ? "  sltu $t4, $t0, $t3\n"
                                            : "  srli.d $t4, $t0, 32\n",
                                 out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            } else if (op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) {
                if (wide) {
                    if (fputs(is_signed ? "  mulh.d $t3, $t0, $t1\n"
                                        : "  mulh.du $t3, $t0, $t1\n", out) == EOF ||
                        fputs("  mul.d $t0, $t0, $t1\n", out) == EOF ||
                        (is_signed &&
                         fputs("  srai.d $t4, $t0, 63\n"
                               "  xor $t3, $t3, $t4\n", out) == EOF) ||
                        fputs("  sltu $t4, $zero, $t3\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                } else if (fputs("  mul.d $t3, $t0, $t1\n"
                                 "  add.w $t0, $t3, $zero\n", out) == EOF ||
                           (is_signed
                            ? fputs("  xor $t4, $t3, $t0\n"
                                    "  sltu $t4, $zero, $t4\n", out) == EOF
                            : fputs("  srli.d $t4, $t3, 32\n"
                                    "  sltu $t4, $zero, $t4\n", out) == EOF))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            } else return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fputs(wide ? "  st.d $t0, $t2, 0\n"
                          : "  st.w $t0, $t2, 0\n", out) == EOF ||
                fputs("  or $t0, $t4, $zero\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
                !emit_sp_mem(out, width == 64U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (width < 32U &&
                fputs(width == 8U
                          ? "  bstrpick.w $t0, $t0, 7, 0\n"
                          : "  bstrpick.w $t0, $t0, 15, 0\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ
                      ? (width == 64U ? "  clz.d $t0, $t0\n"
                                       : "  clz.w $t0, $t0\n")
                      : (width == 64U ? "  ctz.d $t0, $t0\n"
                                       : "  ctz.w $t0, $t0\n"), out) == EOF ||
                (width < 32U &&
                 ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
                 fprintf(out, "  addi.w $t0, $t0, -%u\n",
                         32U - width) < 0))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
        case MINIC_CORE_INSTRUCTION_CALL:
        case MINIC_CORE_INSTRUCTION_INDIRECT_CALL: {
            const bool indirect = ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL;
            const size_t begin = indirect ? ins->value.indirect_call.argument_begin
                                          : ins->value.call.argument_begin;
            const size_t count = indirect ? ins->value.indirect_call.argument_count
                                          : ins->value.call.argument_count;
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
            size_t j;
            size_t stack_bytes;
            MinicLP64ArgumentPlan call_plan;
            /* Resolve ABI locations once before allocating outgoing stack
             * space; the same plan drives each argument's source cursor. */
            if (!minic_lp64_plan_direct_call(
                    program, target, fn, ins,
                    MINIC_LP64_ABI_LOONGARCH64_LP64D,
                    minic_multi_indirect_record,
                    &call_plan))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            stack_bytes = call_plan.stack_bytes;
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, false))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                const MinicLP64ArgumentLocation *location = &call_plan.arguments[j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (!minic_lp64_frame_plan_object(frame_plan, object_id, &object_offset))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                              reject_stage, reject_block, reject_ins, __LINE__);
                    if (!minic_multi_gp_record(program, target,
                            fn->objects[object_id].type, &record_size, &slots)) {
                        const size_t src = object_offset +
                            (dynamic_frame ? 0U : stack_bytes);
                        if (!minic_multi_indirect_record(program, target,
                                fn->objects[object_id].type, &record_size) ||
                            fprintf(out, "  li.d $t0, %zu\n"
                                         "  add.d $t0, %s, $t0\n",
                                    src, dynamic_frame ? "$s0" : "$sp") < 0 ||
                            (location->gp_registers != 0U
                             ? fprintf(out, "  move $a%zu, $t0\n", location->gp_first) < 0
                             : !minic_loongarch64_emit_frame_mem(out, "st.d", "$t0",
                                               location->stack_first * 8U, false)))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const bool in_reg = chunk < location->gp_registers;
                        const size_t stack_slot = in_reg ? 0U :
                            location->stack_first + chunk - location->gp_registers;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, object_offset + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U,
                                payload, in_reg, in_reg ? location->gp_first + chunk : 0U,
                                stack_slot, dynamic_frame))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                  reject_stage, reject_block, reject_ins, __LINE__);
                    }
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U +
                    (dynamic_frame ? 0U : stack_bytes);
                if (minic_type_is_long_double(type)) {
                    size_t src, chunk;
                    const size_t used = location->gp_registers;
                    if (!minic_lp64_frame_plan_wide_slot(frame_plan, arg->value.value_id, &src))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                    for (chunk = 0U; chunk < 2U; ++chunk) {
                        if (chunk < used) {
                            if (!emit_sp_mem(out, "ld.d", gp_regs[location->gp_first + chunk],
                                             src + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U))
                                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                           reject_stage, reject_block,
                                                           reject_ins, __LINE__);
                        } else if (!emit_sp_mem(out, "ld.d", "$t0",
                                                src + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U) ||
                                   !minic_loongarch64_emit_frame_mem(out, "st.d", "$t0",
                                                   (location->stack_first + chunk - used) * 8U,
                                                   false))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    }
                    continue;
                }
                if (location->fp_registers != 0U) {
                    if (!emit_sp_mem(out, minic_type_is_float(type)
                                           ? "fld.s" : "fld.d",
                                     fp_regs[location->fp_first], slot))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                } else {
                    const bool wide = minic_type_is_double(type) ||
                        minic_lp64_register64_type(program, target, type);
                    if (location->gp_registers != 0U) {
                        if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                                         gp_regs[location->gp_first], slot))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    } else if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                                            "$t0", slot) ||
                               /* The LoongArch LP64D stack argument slot is
                                * 8 bytes even for 32-bit int/unsigned int.
                                * ld.w sign-extends both (ABI requirement),
                                * so st.d must write all 64 bits. st.w left
                                * the upper half uninitialized and could
                                * poison a variadic callee's 8-byte va slot. */
                               !minic_loongarch64_emit_frame_mem(out, "st.d",
                                            "$t0", location->stack_first * 8U, false))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                }
            }
            if (indirect) {
                if (!emit_sp_mem(out, "ld.d", "$t2",
                                 (size_t)ins->value.indirect_call.callee * 8U +
                                 (dynamic_frame ? 0U : stack_bytes)) ||
                    fputs("  jirl $ra, $t2, 0\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            } else {
                const MinicCoreCallee *callee =
                    &fn->callees[ins->value.call.callee_id];
                if (fprintf(out, "  bl %s\n", callee->name) < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            }
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, true))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
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
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!minic_loongarch64_emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U, dynamic_frame))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst) ||
                    !emit_sp_mem(out, "st.d", "$a0", dst) ||
                    !emit_sp_mem(out, "st.d", "$a1", dst + 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                continue;
            }
            if (fputs(minic_type_is_double(ins->type)
                          ? "  movfr2gr.d $t0, $fa0\n"
                          : minic_type_is_float(ins->type)
                              ? "  movfr2gr.s $t0, $fa0\n"
                          : "  or $t0, $a0, $zero\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) ||
                    (width != 8U && !emit_integer_normalize(out, width, sign)))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
            }
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
        case MINIC_CORE_INSTRUCTION_PARAMETER: {
            const size_t ordinal = (size_t)ins->value.parameter_index;
            static const char *const gp_regs[8] = {
                "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"};
            static const char *const fp_regs[8] = {
                "$fa0", "$fa1", "$fa2", "$fa3", "$fa4", "$fa5", "$fa6", "$fa7"};
            const MinicLP64ArgumentLocation *location =
                ordinal < incoming_plan->argument_count
                    ? &incoming_plan->arguments[ordinal] : NULL;
            if (location == NULL || !minic_type_equal(ins->type,
                                                    fn->parameter_types[ordinal]) ||
                location->kind == MINIC_LP64_ARGUMENT_RECORD ||
                location->kind == MINIC_LP64_ARGUMENT_INDIRECT_RECORD)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            if (minic_type_is_long_double(ins->type)) {
                size_t dst, chunk;
                const size_t used = location->gp_registers;
                if (!minic_lp64_frame_plan_wide_slot(frame_plan, ins->result, &dst))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (chunk = 0U; chunk < 2U; ++chunk) {
                    if (chunk < used) {
                        if (!emit_sp_mem(out, "st.d", gp_regs[location->gp_first + chunk],
                                         dst + chunk * 8U))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    } else if (!emit_sp_mem(out, "ld.d", "$t0",
                                            frame + (location->stack_first + chunk - used) * 8U) ||
                               !emit_sp_mem(out, "st.d", "$t0",
                                            dst + chunk * 8U))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if ((minic_type_is_double(ins->type) ||
                 minic_type_is_float(ins->type)) && location->fp_registers != 0U) {
                if (!emit_sp_mem(out, minic_type_is_float(ins->type)
                                      ? "fst.s" : "fst.d",
                                 fp_regs[location->fp_first], offset))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            } else if (location->gp_registers != 0U) {
                if (!emit_sp_mem(out,
                                 minic_lp64_register64_type(program, target, ins->type) ||
                                 minic_type_is_double(ins->type) ? "st.d" : "st.w",
                                 gp_regs[location->gp_first], offset))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            } else {
                const char *const load =
                    minic_lp64_register64_type(program, target, ins->type) ||
                    minic_type_is_double(ins->type) ? "ld.d" : "ld.w";
                const char *const store =
                    minic_lp64_register64_type(program, target, ins->type) ||
                    minic_type_is_double(ins->type) ? "st.d" : "st.w";
                if (!emit_sp_mem(out, load, "$t0",
                                 frame + location->stack_first * 8U) ||
                    !emit_sp_mem(out, store, "$t0", offset))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            }
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
                    !emit_sp_mem(out, "ld.d", "$t0", src_offset) ||
                    !emit_sp_mem(out, "ld.d", "$t1", src_offset + 8U) ||
                    !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                                 "$t2", (size_t)rhs * 8U) ||
                    fputs("  beqz $t2, 2f\n"
                          "  li.d $t4, 64\n"
                          "  bgeu $t2, $t4, 1f\n"
                          "  sub.d $t4, $zero, $t2\n"
                          "  sll.d $t4, $t1, $t4\n"
                          "  srl.d $t0, $t0, $t2\n"
                          "  or $t0, $t0, $t4\n", out) == EOF ||
                    fputs(sign ? "  sra.d $t1, $t1, $t2\n" :
                                 "  srl.d $t1, $t1, $t2\n", out) == EOF ||
                    fputs("  b 2f\n"
                          "1:\n"
                          "  addi.d $t2, $t2, -64\n", out) == EOF ||
                    fputs(sign ? "  sra.d $t0, $t1, $t2\n"
                                 "  srai.d $t1, $t1, 63\n" :
                                 "  srl.d $t0, $t1, $t2\n"
                                 "  move $t1, $zero\n", out) == EOF ||
                    fputs("2:\n", out) == EOF ||
                    !emit_sp_mem(out, "st.d", "$t0", dst_offset) ||
                    !emit_sp_mem(out, "st.d", "$t1", dst_offset + 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
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
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                    ? (wide ? "  sll.d $t0, $t0, $t1\n"
                            : "  sll.w $t0, $t0, $t1\n")
                    : (ls ? (wide ? "  sra.d $t0, $t0, $t1\n"
                                  : "  sra.w $t0, $t0, $t1\n")
                          : (wide ? "  srl.d $t0, $t0, $t1\n"
                                  : "  srl.w $t0, $t0, $t1\n"));
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                /* Signed narrow shift inputs require extension before sra. */
                (lw < 4U && !emit_integer_normalize(out, lw, ls)) ||
                !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            (void)rs;
            break;
        }
        /* Original cJSON uses ~ on signed/unsigned scalar values.
         * NOR against zero is the LoongArch native complement; normalize
         * subword results before storing the Core SSA slot. */
        case MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U) ||
                fputs("  nor $t0, $t0, $zero\n", out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            break;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_NEGATE: {
            const MinicCoreValueId operand = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_integer_unary_shape(
                    program, target, fn, ins, &width, &sign) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)operand * 8U) ||
                fputs(width == 8U ? "  sub.d $t0, $zero, $t0\n"
                                  : "  sub.w $t0, $zero, $t0\n", out) == EOF ||
                (width != 8U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            wide = lw == 8U;
            /* M36: real LP64 multiplication uses the full 64-bit operands. */
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_ADD
                ? (wide ? "  add.d $t0, $t0, $t1\n" : "  add.w $t0, $t0, $t1\n")
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT
                    ? (wide ? "  sub.d $t0, $t0, $t1\n" : "  sub.w $t0, $t0, $t1\n")
                    : (wide ? "  mul.d $t0, $t0, $t1\n" : "  mul.w $t0, $t0, $t1\n");
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF ||
                (!wide && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
            const char *opcode;
            if (!minic_multi_direct_integer_binary_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            opcode = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND
                ? "  and $t0, $t0, $t1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR
                    ? "  xor $t0, $t0, $t1\n"
                    : "  or $t0, $t0, $t1\n";
            if (!emit_sp_mem(out, lw == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, rw == 8U ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(opcode, out) == EOF ||
                (lw != 8U && !emit_integer_normalize(out, lw, ls)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                    fn, "loongarch64", name, reject_stage, reject_block,
                    reject_ins, __LINE__);
            wide = lw == 8U;
            op = ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE
                ? (wide ? (ls ? "  div.d $t0, $t0, $t1\n"
                               : "  div.du $t0, $t0, $t1\n")
                        : (ls ? "  div.w $t0, $t0, $t1\n"
                               : "  div.wu $t0, $t0, $t1\n"))
                : (wide ? (ls ? "  mod.d $t0, $t0, $t1\n"
                               : "  mod.du $t0, $t0, $t1\n")
                        : (ls ? "  mod.w $t0, $t0, $t1\n"
                               : "  mod.wu $t0, $t0, $t1\n"));
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                             "$t1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            break;
        }
        /* M33: unsigned 64-bit pointer order for cJSON overlap checks.
         * Operand pointer equivalence is also enforced in Core IR. */
        case MINIC_CORE_INSTRUCTION_POINTER_LESS: {
            const MinicCoreValueId lhs = ins->value.binary.left;
            const MinicCoreValueId rhs = ins->value.binary.right;
            if (!minic_multi_direct_pointer_less_shape(fn, ins) ||
                !emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                fputs("  sltu $t0, $t0, $t1\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                if (!emit_sp_mem(out, "ld.d", "$t0", (size_t)lhs * 8U) ||
                    !emit_sp_mem(out, "ld.d", "$t1", (size_t)rhs * 8U) ||
                    fputs("  xor $t0, $t0, $t1\n"
                          "  sltui $t0, $t0, 1\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (!minic_multi_direct_integer_compare_shape(
                    program, target, fn, ins, &lw, &rw, &ls, &rs))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = lw == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)lhs * 8U) ||
                !emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t1",
                             (size_t)rhs * 8U) ||
                fputs(ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_LESS
                          ? (ls ? "  slt $t0, $t0, $t1\n"
                                : "  sltu $t0, $t0, $t1\n")
                          : "  xor $t0, $t0, $t1\n  sltui $t0, $t0, 1\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            wide = pointer || width == 8U;
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)v * 8U) ||
                fputs("  sltui $t0, $t0, 1\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            (void)sign;
            break;
        }
        /* M28: IEEE integer/double conversions used by cJSON's saturated
         * valueint assignment. Only signed 32-bit conversion is admitted. */
        /* M31: numeric IEEE binary32->binary64 promotion for float arrays.
         * Reinterpretation/bitcast is incorrect: perform fcvt.d.s. */
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT: {
            const MinicCoreValueId source = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.d", "$ft0", (size_t)source * 8U) ||
                fputs("  fcvt.s.d $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.s", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
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
                ? "  fadd.s $ft0, $ft0, $ft1\n"
                : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                    ? "  fsub.s $ft0, $ft0, $ft1\n"
                    : ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                        ? "  fmul.s $ft0, $ft0, $ft1\n"
                        : "  fdiv.s $ft0, $ft0, $ft1\n";
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.s", "$ft0", (size_t)lhs * 8U) ||
                !emit_sp_mem(out, "fld.s", "$ft1", (size_t)rhs * 8U) ||
                fputs(op, out) == EOF ||
                !emit_sp_mem(out, "fst.s", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, NULL, NULL) ||
                !emit_sp_mem(out, "fld.s", "$ft0", (size_t)src * 8U) ||
                fputs("  fcvt.d.s $ft0, $ft0\n", out) == EOF ||
                !emit_sp_mem(out, "fst.d", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign) ||
                !emit_sp_mem(out, width == 8U ? "ld.d" : "ld.w",
                             "$t0", (size_t)src * 8U) ||
                (width < 4U && !emit_integer_normalize(out, width, sign)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (width == 8U && !sign) {
                /* LoongArch base FP has signed ffint.d.l, but not an
                 * unsigned 64-bit scalar conversion. Split at 32 bits:
                 * both pieces are exact in FP64, and fadd rounds ONCE.
                 * This avoids the double-rounding trap of (x>>1)*2. */
                if (fputs("  srli.d $t1, $t0, 32\n"
                          "  bstrpick.d $t0, $t0, 31, 0\n"
                          "  movgr2fr.d $ft0, $t1\n"
                          "  ffint.d.l $ft0, $ft0\n"
                          "  movgr2fr.d $ft1, $t0\n"
                          "  ffint.d.l $ft1, $ft1\n"
                          "  li.d $t2, 0x41f0000000000000\n"
                          "  movgr2fr.d $ft2, $t2\n"
                          "  fmul.d $ft0, $ft0, $ft2\n"
                          "  fadd.d $ft0, $ft0, $ft1\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                        reject_block, reject_ins, __LINE__);
            } else if (width == 4U && !sign) {
                /* ld.w sign-extends on LP64; zero-extend unsigned int
                 * before signed-long-to-double conversion. */
                if (fputs("  bstrpick.d $t0, $t0, 31, 0\n"
                          "  movgr2fr.d $ft0, $t0\n"
                          "  ffint.d.l $ft0, $ft0\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                        reject_block, reject_ins, __LINE__);
            } else {
                if (fputs(width == 8U
                              ? "  movgr2fr.d $ft0, $t0\n"
                                "  ffint.d.l $ft0, $ft0\n"
                              : "  movgr2fr.w $ft0, $t0\n"
                                "  ffint.d.w $ft0, $ft0\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                        reject_block, reject_ins, __LINE__);
            }
            if (!emit_sp_mem(out, "fst.d", "$ft0", offset))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        case MINIC_CORE_INSTRUCTION_DOUBLE_TO_INTEGER: {
            const MinicCoreValueId src = ins->value.operand;
            size_t width;
            bool sign;
            const char *conversion;
            /* LoongArch's ftintrz.l.d converts binary64 to signed64.
             * That also covers the entire unsigned32 range: retain its
             * low 32 bits for the Core result. Unsigned64 remains gated. */
            if (!minic_multi_direct_floating_shape(
                    program, target, fn, ins, &width, &sign))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            if (width == 8U && !sign) {
                /* LP64D supplies signed ftintrz.l.d only. For C-defined
                 * uint64 conversions, translate the upper half of the
                 * unsigned domain by 2^63 and restore its high bit. */
                if (!emit_sp_mem(out, "fld.d", "$ft0",
                                 (size_t)src * 8U) ||
                    fprintf(out,
                       "  li.d $t1, 0x43e0000000000000\n"
                       "  movgr2fr.d $ft1, $t1\n"
                       "  fcmp.clt.d $fcc0, $ft0, $ft1\n"
                       "  movcf2gr $t1, $fcc0\n"
                       "  bnez $t1, .Lmini_loong_u64_f%zu_v%u_low\n"
                       "  fsub.d $ft0, $ft0, $ft1\n"
                       "  ftintrz.l.d $ft0, $ft0\n"
                       "  movfr2gr.d $t0, $ft0\n"
                       "  li.d $t1, 0x8000000000000000\n"
                       "  or $t0, $t0, $t1\n"
                       "  b .Lmini_loong_u64_f%zu_v%u_done\n"
                       ".Lmini_loong_u64_f%zu_v%u_low:\n"
                       "  ftintrz.l.d $ft0, $ft0\n"
                       "  movfr2gr.d $t0, $ft0\n"
                       ".Lmini_loong_u64_f%zu_v%u_done:\n",
                       id, (unsigned int)ins->result,
                       id, (unsigned int)ins->result,
                       id, (unsigned int)ins->result,
                       id, (unsigned int)ins->result) < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                break;
            }
            conversion = width == 4U && sign
                ? "  ftintrz.w.d $ft0, $ft0\n"
                  "  movfr2gr.s $t0, $ft0\n"
                : "  ftintrz.l.d $ft0, $ft0\n"
                  "  movfr2gr.d $t0, $ft0\n";
            if (!emit_sp_mem(out, "fld.d", "$ft0", (size_t)src * 8U) ||
                fputs(conversion, out) == EOF ||
                (width == 4U && !sign &&
                 fputs("  bstrpick.d $t0, $t0, 31, 0\n", out) == EOF) ||
                (width < 4U && !emit_integer_normalize(out, width, sign)))
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
