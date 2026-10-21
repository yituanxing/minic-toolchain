#include "target/loongarch64/codegen.h"
#include "target/common/global_emit.h"
#include "target/common/direct_program_emit.h"
#include "target/common/direct_preflight.h"
#include "target/common/direct_call_preflight.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_frame.h"
#include "target/common/lp64_aggregate.h"
#include "target/loongarch64/frame_emit.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* M1/M2: prove correct *actual* GNU ELF/ABI assembly, not just target naming.
 * Each signed 32-bit Core SSA value gets an 8-byte private stack slot.
 * Scalar int parameters, arithmetic, and basic block branches are admitted.
 * No implicit ABI equivalence with RISC-V64/x86-64 is assumed. */


/* Report the first failed backend guard only in opt-in trace mode.
 * backend_line points to the precise C condition; block/ins/kind to Core IR.
 * This leaves compilation and fail-closed semantics unchanged. */




/* Comparisons are Core booleans, not 32-bit C int. Both still fit our
 * private 8-byte SSA slots, with canonical 0/1 in the low 32 bits. */




/* M8: x86's width/sign principle, target-owned machine encodings.
 * A raw Core slot reserves 8 bytes even when the C object is 1 or 2 bytes.
 * The operand width comes from the target DataLayout, not a hard-coded
 * assumption about typedef names or host char signedness. */


/* M20 LP64 scalar-width gate: only ABI and memory operations accept 64 bits.
 * Narrow arithmetic continues to reject 64-bit values until implemented. */




/* C scalar return ABI is independent from the initial signed-int smoke
 * gate. Core's unsigned char xtime return and its GNU callers must share
 * the actual target integer width; reject unsupported 64/128-bit values. */


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

static bool emit_lp64_record_chunk_parameter(FILE *out, size_t offset,
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

/* Macro scoped to emit_function: keep all existing fixed Core frame
 * accesses explicit, but distinguish them from outgoing SP slots. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

static bool emit_function(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    const char *name = minic_c0_function_symbol_name(decl);
    const MinicCoreBlock *block;
    size_t frame = 0U, i, object_end, save_offset, ld_base;
    bool has_direct_calls = false;
    bool dynamic_frame = false;
    const char *reject_stage = "function_preflight";
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX;
    size_t va_count = 0U, va_gp = 0U;
    /* No semantic change: source-specific diagnostics only for the active
     * cJSON failure, never for normal builds or other projects. */
    const bool trace_cjson = getenv("MINIC_M1_TRACE") != NULL &&
                             getenv("MINIC_BACKEND_CENSUS") == NULL &&
                             name != NULL &&
                             (strcmp(name, "cJSON_ParseWithLengthOpts") == 0 ||
                               strcmp(name, "cJSON_Compare") == 0 ||
                               strcmp(name, "parse_number") == 0 ||
                               strcmp(name, "ensure") == 0 ||
                               strcmp(name, "compare_double") == 0 ||
                               strcmp(name, "print_number") == 0);
    if (trace_cjson)
        (void)fprintf(stderr,
            "MINIC_CJSON_STAGE arch=loongarch64 function=%s stage=entry blocks=%zu values=%zu objects=%zu params=%zu calls=%zu\n",
            name, fn->block_count, fn->value_count, fn->object_count,
            fn->parameter_count, fn->callee_count);
    if (getenv("MINIC_M1_TRACE") != NULL &&
        name != NULL && ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr,
                      "MINIC_M9_GATE arch=loongarch64 fn=%s stage=entry values=%zu objects=%zu blocks=%zu params=%zu return_valid=%d inline=%zu\n",
                      name, fn->value_count, fn->object_count,
                      fn->block_count, fn->parameter_count,
                      minic_lp64_valid_return_type(program, target, fn->return_type) ? 1 : 0,
                      fn->inline_asm_count);
    if (!minic_multi_direct_lp64_function_preflight(program, target, decl, fn,
                                               "loongarch64", name))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
            reject_stage, reject_block, reject_ins, __LINE__);
    /* LoongArch lp64d variadic gp cursor is a linear 8-byte register
     * save area followed immediately by overflow arguments. */
    if (decl->is_variadic) {
        for (i = 0U; i < fn->parameter_count; ++i) {
            if (minic_type_is_double(fn->parameter_types[i]))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           "variadic_fp_fixed", SIZE_MAX,
                                           SIZE_MAX, __LINE__);
            ++va_gp;
        }
    }
    for (i = 0U; i < fn->instruction_count; ++i) {
        if (fn->instructions[i].kind ==
            MINIC_CORE_INSTRUCTION_VARIADIC_ARGUMENT_ADDRESS) ++va_count;
        if (fn->instructions[i].kind ==
            MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC) dynamic_frame = true;
    }
    if (va_count != 0U &&
        (!decl->is_variadic || va_count != 1U || va_gp > 8U))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                   "variadic_preflight", SIZE_MAX,
                                   SIZE_MAX, __LINE__);
    for (i = 0U; i < fn->parameter_count; ++i) {
        size_t record_size, record_slots;
        if (!minic_lp64_valid_parameter_type(program, target, fn->parameter_types[i]) &&
            !minic_multi_gp_record(program, target, fn->parameter_types[i],
                                   &record_size, &record_slots) &&
            !minic_multi_indirect_record(program, target,
                                         fn->parameter_types[i], &record_size))
            return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                       reject_block, reject_ins, __LINE__);
    }
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=parameters_valid\n", name);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=parameters_ok\n");
    reject_stage = "cfg_preflight";
    if (!minic_multi_direct_lp64_cfg_preflight(program, target, fn,
                                         "loongarch64", name, trace_cjson,
                                         &reject_block))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
            reject_stage, reject_block, reject_ins, __LINE__);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=cfg_ok\n");
    /* Object byte extents and alignment are validated by minic_lp64_frame_objects.
     * This admits real repeated local arrays without overlapping slots. */
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=terminators_valid\n", name);
    reject_stage = "frame_layout";
    reject_block = SIZE_MAX;
    if (!minic_lp64_frame_objects(program, target, fn, fn->object_count, NULL,
                       &object_end)) {
        if (getenv("MINIC_M1_TRACE") != NULL)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_REJECT arch=loongarch64 function=%s values=%zu objects=%zu\n",
                          name, fn->value_count, fn->object_count);
        return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=frame_objects_ok end=%zu\n", object_end);
    ld_base = (object_end + 15U) & ~(size_t)15U;
    if (!minic_lp64_binary128_slot(fn, ld_base, (MinicCoreValueId)fn->value_count,
                        &object_end))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                   "binary128_layout", SIZE_MAX, SIZE_MAX, __LINE__);
    /* M24: direct and first-class indirect scalar calls share the same
     * eight integer-register ABI. Aggregate, FP, overflow arguments and
     * unsupported variadic tail types are still rejected before emission. */
    reject_stage = "call_preflight";
    if (!minic_multi_direct_lp64_calls_preflight(
            program, target, fn, "loongarch64", name,
            minic_multi_indirect_record,
            &reject_ins, &has_direct_calls))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                reject_stage, reject_block,
                                                reject_ins, __LINE__);
    if (getenv("MINIC_M1_TRACE") != NULL &&
        ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
        (void)fprintf(stderr, "MINIC_M9_GATE arch=loongarch64 fn=%s stage=calls_valid object_end=%zu\n",
                      name, object_end);
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=calls_ok\n");
    reject_stage = "frame_finalization";
    reject_ins = SIZE_MAX;
    if (!minic_lp64_finalize_direct_frame(
            object_end, has_direct_calls, dynamic_frame, va_count != 0U,
            MINIC_LP64_FRAME_LOONGARCH64_LP64D,
            &save_offset, NULL, &frame)) {
        if (getenv("MINIC_M1_TRACE") != NULL && frame > MINIC_LARGE_FRAME_MAX)
            (void)fprintf(stderr,
                          "MINIC_M8_FRAME_CAP arch=loongarch64 function=%s frame=%zu end=%zu\n",
                          name, frame, object_end);
        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                reject_stage, reject_block,
                                                reject_ins, __LINE__);
    }
    if (trace_cjson) (void)fprintf(stderr, "MINIC_CJSON_STAGE arch=loongarch64 stage=machine_emit frame=%zu\n", frame);
    if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",@progbits\n", name) < 0)
            return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    } else if (fprintf(out, ".text\n") < 0) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal && fprintf(out, ".globl %s\n", name) < 0)
        return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal &&
        !minic_multi_emit_elf_symbol_visibility(out, decl->visibility, name))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (fprintf(out, ".p2align 2\n.type %s,@function\n%s:\n", name, name) < 0)
        return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!minic_loongarch64_emit_sp_adjust(out, frame, false) ||
        (dynamic_frame &&
         (!minic_loongarch64_emit_frame_mem(out, "st.d", "$s0",
                         save_offset + (has_direct_calls ? 8U : 0U), false) ||
          fputs("  move $s0, $sp\n", out) == EOF)))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (has_direct_calls &&
        !emit_sp_mem(out, "st.d", "$ra", save_offset)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (va_count != 0U) {
        static const char *const regs[8] = {
            "$a0", "$a1", "$a2", "$a3",
            "$a4", "$a5", "$a6", "$a7"};
        for (i = 0U; i < 8U; ++i)
            if (!emit_sp_mem(out, "st.d", regs[i],
                             frame - 64U + i * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           "variadic_save", SIZE_MAX,
                                           SIZE_MAX, __LINE__);
    }
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
        if (trace_cjson)
            (void)fprintf(stderr,
                "MINIC_CJSON_STEP arch=loongarch64 function=%s block=%zu ins=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                name, block_id, (unsigned int)ins_id, (int)ins->kind,
                (unsigned int)ins->result,
                minic_type_is_integer(ins->type) ? 1 : 0,
                minic_type_is_pointer(ins->type) ? 1 : 0);
        /* Only recorded in the temporary original-source failure log,
         * never in normal builds or public diagnostic stream. */
        if (getenv("MINIC_M1_TRACE") != NULL &&
            ((name[0] == 'K' && name[1] == 'e') || (name[0] == 'A' && name[1] == 'd')))
            (void)fprintf(stderr,
                          "MINIC_M9_STEP arch=loongarch64 function=%s block=%zu ins=%u kind=%d result=%u type_int=%d type_ptr=%d\n",
                          name, block_id, (unsigned int)ins_id,
                          (int)ins->kind, (unsigned int)ins->result,
                          minic_type_is_integer(ins->type) ? 1 : 0,
                          minic_type_is_pointer(ins->type) ? 1 : 0);
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
            const MinicCoreValueId dst = ins->value.record_copy.destination_address;
            const MinicCoreValueId src = ins->value.record_copy.source_address;
            MinicType dst_type, src_type, dst_unqualified, src_unqualified;
            size_t bytes, alignment;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                !minic_type_is_record(ins->type) ||
                dst >= fn->value_count || src >= fn->value_count ||
                !minic_type_pointee(fn->values[dst].type, &dst_type) ||
                !minic_type_pointee(fn->values[src].type, &src_type) ||
                !minic_type_unqualified(dst_type, &dst_unqualified) ||
                !minic_type_unqualified(src_type, &src_unqualified) ||
                !minic_type_equal(dst_unqualified, ins->type) ||
                !minic_type_equal(src_unqualified, ins->type) ||
                !minic_data_layout_type(target->data_layout, program, ins->type,
                                        &bytes, &alignment) ||
                alignment == 0U || bytes > 2048U)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)dst * 8U) ||
                !emit_sp_mem(out, "ld.d", "$t2", (size_t)src * 8U))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            for (size_t byte = 0U; byte < bytes; ++byte)
                if (fputs("  ld.bu $t0, $t2, 0\n"
                          "  st.b $t0, $t1, 0\n"
                          "  addi.d $t2, $t2, 1\n"
                          "  addi.d $t1, $t1, 1\n", out) == EOF)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if (ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT) {
            const size_t position = ins->value.parameter_object.parameter_index;
            const MinicCoreObjectId obj_id = ins->value.parameter_object.object_id;
            size_t size = 0U, slots = 0U, object_offset, gp = 0U, fp = 0U;
            size_t stack = 0U, k, chunk;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                position >= fn->parameter_count ||
                obj_id >= fn->object_count ||
                !minic_type_equal(fn->objects[obj_id].type,
                                  fn->parameter_types[position]) ||
                fn->objects[obj_id].element_count != 1U ||
                (!minic_multi_gp_record(program, target,
                         fn->parameter_types[position], &size, &slots) &&
                 !minic_multi_indirect_record(program, target,
                         fn->parameter_types[position], &size)) ||
                !minic_lp64_frame_objects(program, target, fn, obj_id,
                               &object_offset, NULL))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            for (k = 0U; k < position; ++k) {
                const MinicType previous = fn->parameter_types[k];
                size_t other_size, other_slots;
                if (minic_multi_gp_record(program, target, previous,
                                          &other_size, &other_slots)) {
                    /* LP64D may split a two-eightbyte record between
                     * its final available GAR and the first stack slot. */
                    const size_t registers = gp < 8U ? 8U - gp : 0U;
                    const size_t used = other_slots < registers
                        ? other_slots : registers;
                    gp += used;
                    stack += other_slots - used;
                } else if (minic_type_is_double(previous) && fp < 8U) {
                    ++fp;
                } else {
                    if (gp < 8U) ++gp; else ++stack;
                }
            }
            if (slots == 0U) {
                if ((gp < 8U
                     ? fprintf(out, "  move $t0, $a%zu\n", gp) < 0
                     : !emit_sp_mem(out, "ld.d", "$t0",
                                    frame + stack * 8U)) ||
                    fprintf(out, "  li.d $t1, %zu\n"
                                 "  add.d $t1, %s, $t1\n",
                            object_offset, dynamic_frame ? "$s0" : "$sp") < 0)
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                for (k = 0U; k < size; ++k) {
                    if (fputs("  ld.bu $t2, $t0, 0\n"
                              "  st.b $t2, $t1, 0\n"
                              "  addi.d $t0, $t0, 1\n"
                              "  addi.d $t1, $t1, 1\n", out) == EOF)
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                            reject_stage, reject_block, reject_ins, __LINE__);
                }
                continue;
            }
            for (chunk = 0U; chunk < slots; ++chunk) {
                const size_t registers = gp < 8U ? 8U - gp : 0U;
                const bool in_reg = chunk < registers;
                const size_t stack_slot = in_reg ? 0U :
                    stack + chunk - registers;
                const size_t payload = size - chunk * 8U >= 8U
                    ? 8U : size - chunk * 8U;
                if (!emit_lp64_record_chunk_parameter(out,
                        object_offset + chunk * 8U, payload,
                        in_reg, gp + chunk, frame + stack_slot * 8U,
                        dynamic_frame))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
            }
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
                !minic_lp64_binary128_slot(fn, ld_base, value, &src) ||
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
                !minic_lp64_binary128_slot(fn, ld_base, value, &src) ||
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
            MinicType pointee;
            size_t width = 0U, pointee_width = 0U, pointee_align = 0U;
            bool sign = false, pointer;
            if (ins->result != MINIC_CORE_VALUE_INVALID ||
                addr >= fn->value_count || value >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            pointer = minic_type_is_pointer(fn->values[value].type);
            /* Ordinary naturally aligned scalar loads/stores are the
             * relaxed atomic primitive on these LP64 ISAs. Preserve the
             * Core memory effect, but reject every stronger memory order
             * and non-lock-free aggregate/FP operand explicitly. */
            if (atomic &&
                (ins->value.atomic_store.memory_order != 0U ||
                 !minic_type_pointee(fn->values[addr].type, &pointee) ||
                 !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
                 !minic_data_layout_type(target->data_layout, program,
                                         pointee, &pointee_width, &pointee_align)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                          reject_block, reject_ins, __LINE__);

            if (!pointer && !minic_type_is_double(fn->values[value].type) &&
                !minic_type_is_float(fn->values[value].type) &&
                !minic_lp64_scalar_integer_width(program, target, fn->values[value].type,
                                      &width, &sign)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            if (minic_type_is_double(fn->values[value].type)) width = 8U;
            if (minic_type_is_float(fn->values[value].type)) width = 4U;
            if (atomic &&
                (minic_type_is_double(fn->values[value].type) ||
                 minic_type_is_float(fn->values[value].type) ||
                 (pointer ? pointee_width != 8U :
                  (width != 1U && width != 2U &&
                   width != 4U && width != 8U) ||
                  width != pointee_width)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                          reject_block, reject_ins, __LINE__);
            (void)pointee_align;
            if (!emit_sp_mem(out, "ld.d", "$t1", (size_t)addr * 8U) ||
                !emit_sp_mem(out, (pointer || width == 8U) ? "ld.d" : "ld.w", "$t0",
                             (size_t)value * 8U) ||
                (pointer ? fputs("  st.d $t0, $t1, 0\n", out) == EOF
                         : !emit_integer_memory(out, true, width, sign))) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            continue;
        }
        if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
             ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
            (minic_type_is_void(ins->type) ||
             (ins->kind == MINIC_CORE_INSTRUCTION_CALL &&
              minic_type_is_record(ins->type) &&
              ins->value.call.result_object != MINIC_CORE_OBJECT_INVALID))) {
            /* Verified in call preflight: effect-only calls
             * has no Core result slot. Fall through into the call case. */
            offset = 0U;
        } else {
            if (ins->result == MINIC_CORE_VALUE_INVALID ||
                ins->result >= fn->value_count ||
                ((ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS ||
                   ins->kind == MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS)
                    ? !minic_type_is_pointer(ins->type)
                    : !minic_type_is_integer(ins->type) &&
                      !minic_type_is_pointer(ins->type) &&
                      !(minic_type_is_float(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_ADD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE)) &&
                      !(minic_type_is_long_double(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL)) &&
                      !(minic_type_is_double(ins->type) &&
                        (ins->kind == MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_LOAD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_ADD ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT ||
                         ins->kind == MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY ||
                         ins->kind == MINIC_CORE_INSTRUCTION_PARAMETER ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_TO_DOUBLE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE ||
                         ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
                         ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL))))
            {
                if (getenv("MINIC_M1_TRACE") != NULL)
                    (void)fprintf(stderr,
                                  "MINIC_M8_RESULT_REJECT arch=loongarch64 function=%s block=%zu instruction=%u kind=%d result=%u type_integer=%d type_pointer=%d\n",
                                  name, block_id, (unsigned int)ins_id,
                                  (int)ins->kind, (unsigned int)ins->result,
                                  minic_type_is_integer(ins->type) ? 1 : 0,
                                  minic_type_is_pointer(ins->type) ? 1 : 0);
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            }
            offset = (size_t)ins->result * 8U;
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
            if (!minic_type_equal(ins->type, minic_type_int()) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
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
            if (!minic_type_is_double(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
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
            if (!minic_type_is_double(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_double(fn->values[lhs].type) ||
                !minic_type_is_double(fn->values[rhs].type) ||
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
            if (!minic_type_is_double(ins->type) ||
                operand >= fn->value_count ||
                !minic_type_is_double(fn->values[operand].type) ||
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
                if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
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
                !minic_lp64_frame_objects(program, target, fn,
                               (size_t)ins->value.object_id,
                               &object_offset, NULL) ||
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
            if (base >= fn->value_count || index >= fn->value_count ||
                !minic_type_is_pointer(fn->values[base].type) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[index].type,
                                     &iw, &signed_index) ||
                /* Use a full-size stride literal: addi.d signed imm12
                 * cannot encode large record/array elements. */
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
            /* Original AES_ECB_encrypt converts uint8_t* to state_t*.
             * Both are LP64 pointer representations: preserve all 64 bits.
             * Do NOT incorrectly turn integer/pointer casts into a no-op. */
            const MinicCoreValueId source = ins->value.operand;
            size_t src_width = 0U;
            bool src_signed = false;
            bool src_pointer;
            /* M35: Core pointer->LP64 integer cast keeps the exact address
             * bit pattern (needed for ptrdiff_t in real cJSON parsing). */
            if (source < fn->value_count &&
                minic_type_is_pointer(fn->values[source].type) &&
                minic_type_is_integer(ins->type)) {
                size_t width;
                bool sign;
                if (!minic_core_scalar_bitcast_types_valid(
                        ins->type, fn->values[source].type) ||
                    !minic_lp64_scalar_integer_width(program, target, ins->type,
                                          &width, &sign) || width != 8U ||
                    !emit_sp_mem(out, "ld.d", "$t0", (size_t)source * 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
                break;
            }
            if (source >= fn->value_count ||
                !minic_type_is_pointer(ins->type) ||
                !minic_core_scalar_bitcast_types_valid(
                    ins->type, fn->values[source].type)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            src_pointer = minic_type_is_pointer(fn->values[source].type);
            if (!src_pointer &&
                !minic_lp64_scalar_integer_width(program, target,
                                      fn->values[source].type,
                                     &src_width, &src_signed)) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
                const MinicCoreValueId addr = ins->value.load.address;
                size_t dst;
                if (addr >= fn->value_count ||
                    !minic_type_is_pointer(fn->values[addr].type) ||
                    !minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
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
                const MinicCoreValueId addr = ins->value.load.address;
                size_t dst;
                if (addr >= fn->value_count ||
                    !minic_type_is_pointer(fn->values[addr].type) ||
                    !minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
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
            MinicType pointee;
            size_t pointee_width = 0U, pointee_align = 0U;
            size_t width = minic_type_is_double(ins->type) ? 8U :
                           minic_type_is_float(ins->type) ? 4U : 0U;
            bool sign = false, pointer = minic_type_is_pointer(ins->type);
            if (atomic &&
                (ins->value.atomic_load.memory_order != 0U ||
                 addr >= fn->value_count ||
                 !minic_type_pointee(fn->values[addr].type, &pointee) ||
                 !(minic_type_is_integer(pointee) || minic_type_is_pointer(pointee)) ||
                 !minic_data_layout_type(target->data_layout, program,
                                         pointee, &pointee_width, &pointee_align) ||
                 (pointer ? pointee_width != 8U :
                     !minic_lp64_scalar_integer_width(program, target, ins->type,
                                           &width, &sign) ||
                     (width != 1U && width != 2U &&
                      width != 4U && width != 8U) ||
                     pointee_width != width)))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                          reject_block, reject_ins, __LINE__);
            (void)pointee_align;
            if (addr >= fn->value_count ||
                !minic_type_is_pointer(fn->values[addr].type) ||
                (!pointer && !minic_type_is_double(ins->type) &&
                 !minic_type_is_float(ins->type) &&
                 !minic_lp64_scalar_integer_width(program, target,
                                       ins->type, &width, &sign)) ||
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
            const size_t parameter_count = indirect
                ? fn->call_signatures[ins->value.indirect_call.signature_id].parameter_count
                : fn->callees[ins->value.call.callee_id].parameter_count;
            const bool is_variadic = indirect
                ? fn->call_signatures[ins->value.indirect_call.signature_id].is_variadic
                : fn->callees[ins->value.call.callee_id].is_variadic;
            size_t j, gp_index = 0U, fp_index = 0U, overflow_slots = 0U;
            size_t stack_index = 0U, stack_bytes;
            /* LP64D unnamed variadic doubles consume GP slots, while fixed
             * FP args use independent fa0-fa7. Overflow slots start at SP.
             * Temporarily lower SP; reload SSA values with the added offset. */
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots;
                    const MinicCoreObject *object =
                        &fn->objects[arg->value.object_id];
                    if (!minic_multi_gp_record(program, target, object->type,
                                               &record_size, &slots)) {
                        if (!minic_multi_indirect_record(program, target,
                                                         object->type, &record_size))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                        if (gp_index++ >= 8U) ++overflow_slots;
                        continue;
                    }
                    const size_t available = gp_index < 8U
                        ? 8U - gp_index : 0U;
                    const size_t used = slots < available ? slots : available;
                    gp_index += used;
                    overflow_slots += slots - used;
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const bool use_fp = (minic_type_is_double(type) ||
                                     minic_type_is_float(type)) &&
                                    (!is_variadic || j < parameter_count) &&
                                    fp_index < 8U;
                if (minic_type_is_long_double(type)) {
                    /* LP64D unnamed binary128 requires an even-numbered
                     * GAR pair, rather than an odd a3/a5/a7 start. */
                    if (is_variadic && j >= parameter_count &&
                        gp_index < 8U && (gp_index & 1U))
                        ++gp_index;
                    const size_t available = gp_index < 8U ? 8U - gp_index : 0U;
                    const size_t used = available < 2U ? available : 2U;
                    if (used == 0U && (overflow_slots & 1U))
                        ++overflow_slots;
                    gp_index += used;
                    overflow_slots += 2U - used;
                } else if (use_fp) {
                    if (fp_index++ >= 8U) ++overflow_slots;
                } else if (gp_index++ >= 8U) ++overflow_slots;
            }
            stack_bytes = (overflow_slots * 8U + 15U) & ~(size_t)15U;
            if (stack_bytes != 0U && !minic_loongarch64_emit_sp_adjust(out, stack_bytes, false))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
                                           reject_ins, __LINE__);
            gp_index = fp_index = 0U;
            for (j = 0U; j < count; ++j) {
                const MinicCoreCallArgument *arg = &fn->call_arguments[begin + j];
                if (arg->kind == MINIC_CORE_CALL_ARGUMENT_OBJECT) {
                    size_t record_size, slots, object_offset, chunk;
                    const MinicCoreObjectId object_id = arg->value.object_id;
                    if (!minic_lp64_frame_objects(program, target, fn, object_id,
                                       &object_offset, NULL))
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
                            (gp_index < 8U
                             ? fprintf(out, "  move $a%zu, $t0\n", gp_index) < 0
                             : !minic_loongarch64_emit_frame_mem(out, "st.d", "$t0",
                                               stack_index * 8U, false)))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                reject_stage, reject_block, reject_ins, __LINE__);
                        if (gp_index++ >= 8U) ++stack_index;
                        continue;
                    }
                    for (chunk = 0U; chunk < slots; ++chunk) {
                        const size_t available = gp_index < 8U
                            ? 8U - gp_index : 0U;
                        const bool in_reg = chunk < available;
                        const size_t stack_slot = in_reg ? 0U :
                            stack_index + chunk - available;
                        const size_t payload = record_size - chunk * 8U >= 8U
                            ? 8U : record_size - chunk * 8U;
                        if (!emit_lp64_record_chunk_argument(
                                out, object_offset + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U,
                                payload, in_reg, gp_index + chunk,
                                stack_slot, dynamic_frame))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                  reject_stage, reject_block, reject_ins, __LINE__);
                    }
                    const size_t available = gp_index < 8U
                        ? 8U - gp_index : 0U;
                    const size_t used = slots < available ? slots : available;
                    gp_index += used;
                    stack_index += slots - used;
                    continue;
                }
                const MinicType type = fn->values[arg->value.value_id].type;
                const size_t slot = (size_t)arg->value.value_id * 8U +
                    (dynamic_frame ? 0U : stack_bytes);
                if (minic_type_is_long_double(type)) {
                    size_t src, chunk;
                    if (is_variadic && j >= parameter_count &&
                        gp_index < 8U && (gp_index & 1U))
                        ++gp_index;
                    const size_t available = gp_index < 8U ? 8U - gp_index : 0U;
                    const size_t used = available < 2U ? available : 2U;
                    if (used == 0U && (stack_index & 1U))
                        ++stack_index;
                    if (!minic_lp64_binary128_slot(fn, ld_base, arg->value.value_id, &src))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                    for (chunk = 0U; chunk < 2U; ++chunk) {
                        if (chunk < used) {
                            if (!emit_sp_mem(out, "ld.d", gp_regs[gp_index + chunk],
                                             src + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U))
                                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                           reject_stage, reject_block,
                                                           reject_ins, __LINE__);
                        } else if (!emit_sp_mem(out, "ld.d", "$t0",
                                                src + (dynamic_frame ? 0U : stack_bytes) + chunk * 8U) ||
                                   !minic_loongarch64_emit_frame_mem(out, "st.d", "$t0",
                                                   (stack_index + chunk - used) * 8U,
                                                   false))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    }
                    gp_index += used;
                    stack_index += 2U - used;
                    continue;
                }
                const bool use_fp = (minic_type_is_double(type) ||
                                     minic_type_is_float(type)) &&
                                    (!is_variadic || j < parameter_count) &&
                                    fp_index < 8U;
                if (use_fp) {
                    if (fp_index < 8U) {
                        if (!emit_sp_mem(out, minic_type_is_float(type)
                                               ? "fld.s" : "fld.d",
                                         fp_regs[fp_index], slot))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    } else if (!emit_sp_mem(out, minic_type_is_float(type)
                                                   ? "fld.s" : "fld.d",
                                            "$ft0", slot) ||
                               !minic_loongarch64_emit_frame_mem(out, minic_type_is_float(type)
                                                   ? "fst.s" : "fst.d",
                                            "$ft0", stack_index++ * 8U, false))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    ++fp_index;
                } else {
                    const bool wide = minic_type_is_double(type) ||
                        minic_lp64_register64_type(program, target, type);
                    if (gp_index < 8U) {
                        if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w",
                                         gp_regs[gp_index], slot))
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
                                            "$t0", stack_index++ * 8U, false))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
                    ++gp_index;
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
                    !minic_lp64_frame_objects(program, target, fn, object_id,
                                   &dst, NULL))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (chunk = 0U; chunk < slots; ++chunk) {
                    const size_t payload = bytes - chunk * 8U >= 8U
                        ? 8U : bytes - chunk * 8U;
                    if (!emit_lp64_record_chunk_parameter(out,
                            dst + chunk * 8U, payload, true, chunk, 0U, dynamic_frame))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst;
                if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst) ||
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
            size_t gp_index = 0U, fp_index = 0U, overflow_index = 0U, k;
            if (ordinal >= fn->parameter_count)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
            /* LoongArch LP64D: when fa0-fa7 are exhausted, even fixed
             * double arguments are passed through remaining a0-a7, then
             * spilled on the stack in source order. */
            for (k = 0U; k < ordinal; ++k) {
                size_t record_bytes = 0U, slots = 0U;
                const MinicType prior = fn->parameter_types[k];
                if (minic_multi_gp_record(program, target, prior,
                                          &record_bytes, &slots)) {
                    /* LP64D passes a two-GP record split across the last
                     * available GAR and stack, unlike AAPCS64. Advance
                     * both cursors before receiving the NEXT scalar. */
                    const size_t available = gp_index < 8U
                        ? 8U - gp_index : 0U;
                    const size_t used = slots < available
                        ? slots : available;
                    gp_index += used;
                    overflow_index += slots - used;
                } else if (minic_type_is_long_double(prior)) {
                    const size_t available = gp_index < 8U ? 8U - gp_index : 0U;
                    const size_t used = available < 2U ? available : 2U;
                    gp_index += used;
                    overflow_index += 2U - used;
                } else if ((minic_type_is_double(prior) ||
                            minic_type_is_float(prior)) && fp_index < 8U)
                    ++fp_index;
                else if (gp_index < 8U) ++gp_index;
                else ++overflow_index;
            }
            if (minic_type_is_long_double(ins->type)) {
                size_t dst, chunk;
                const size_t available = gp_index < 8U ? 8U - gp_index : 0U;
                const size_t used = available < 2U ? available : 2U;
                if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
                for (chunk = 0U; chunk < 2U; ++chunk) {
                    if (chunk < used) {
                        if (!emit_sp_mem(out, "st.d", gp_regs[gp_index + chunk],
                                         dst + chunk * 8U))
                            return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                       reject_stage, reject_block,
                                                       reject_ins, __LINE__);
                    } else if (!emit_sp_mem(out, "ld.d", "$t0",
                                            frame + (overflow_index + chunk - used) * 8U) ||
                               !emit_sp_mem(out, "st.d", "$t0",
                                            dst + chunk * 8U))
                        return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                                   reject_stage, reject_block,
                                                   reject_ins, __LINE__);
                }
                continue;
            }
            if ((minic_type_is_double(ins->type) ||
                 minic_type_is_float(ins->type)) && fp_index < 8U) {
                if (!emit_sp_mem(out, minic_type_is_float(ins->type)
                                      ? "fst.s" : "fst.d",
                                 fp_regs[fp_index], offset))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                    reject_stage, reject_block, reject_ins, __LINE__);
            } else if (gp_index < 8U) {
                if (!emit_sp_mem(out,
                                 minic_lp64_register64_type(program, target, ins->type) ||
                                 minic_type_is_double(ins->type) ? "st.d" : "st.w",
                                 gp_regs[gp_index], offset))
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
                                 frame + overflow_index * 8U) ||
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
                    !minic_lp64_binary128_slot(fn, ld_base, lhs, &src_offset) ||
                    !minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst_offset) ||
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
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                (lw != 1U && lw != 2U && lw != 4U && lw != 8U) ||
                /* C permits 8/16-bit shift-count values after promotion;
                 * the target consumes their low 32-bit SSA slot. */
                minic_type_is_bool_integer(ins->type))
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
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
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
            if (operand >= fn->value_count ||
                !minic_type_equal(fn->values[operand].type, ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type, &width, &sign) ||
                minic_type_is_bool_integer(ins->type) ||
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
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type) ||
                minic_type_is_bool_integer(ins->type))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                lw != rw || (lw != 1U && lw != 2U && lw != 4U && lw != 8U) ||
                ls != rs || minic_type_is_bool_integer(ins->type))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                           reject_block, reject_ins, __LINE__);
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
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type,
                                      &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type,
                                      &rw, &rs) ||
                lw != rw || (lw != 4U && lw != 8U) || ls != rs ||
                !minic_type_equal(fn->values[lhs].type, ins->type))
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                           reject_stage, reject_block,
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
            if (!minic_type_equal(ins->type, minic_type_int()) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_pointer(fn->values[lhs].type) ||
                !minic_type_equal(fn->values[lhs].type, fn->values[rhs].type) ||
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
            if (lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[lhs].type, &lw, &ls) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[rhs].type, &rw, &rs) ||
                lw != rw || ls != rs || (lw != 4U && lw != 8U))
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
            if (v >= fn->value_count) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            pointer = minic_type_is_pointer(fn->values[v].type);
            if (!pointer && !minic_type_equal(fn->values[v].type, minic_type_bool()) &&
                !minic_lp64_scalar_integer_width(program, target, fn->values[v].type,
                                      &width, &sign))
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
            if (!minic_type_is_float(ins->type) ||
                source >= fn->value_count ||
                !minic_type_is_double(fn->values[source].type) ||
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
            if (!minic_type_is_float(ins->type) ||
                lhs >= fn->value_count || rhs >= fn->value_count ||
                !minic_type_is_float(fn->values[lhs].type) ||
                !minic_type_is_float(fn->values[rhs].type) ||
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
            if (src >= fn->value_count || !minic_type_is_double(ins->type) ||
                !minic_type_is_float(fn->values[src].type) ||
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
            if (src >= fn->value_count || !minic_type_is_double(ins->type) ||
                !minic_lp64_scalar_integer_width(program, target, fn->values[src].type,
                                      &width, &sign) ||
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
            if (src >= fn->value_count ||
                !minic_type_is_double(fn->values[src].type) ||
                !minic_lp64_scalar_integer_width(program, target, ins->type, &width, &sign) ||
                (width != 1U && width != 2U &&
                 width != 4U && width != 8U))
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
                    if (!minic_lp64_binary128_slot(fn, ld_base, src, &src_offset) ||
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
                    if (!minic_lp64_binary128_slot(fn, ld_base, ins->result, &dst_offset) ||
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
        switch (block->terminator.kind) {
        case MINIC_CORE_TERMINATOR_RETURN:
            if (minic_type_is_long_double(fn->return_type)) {
                size_t src;
                if (!minic_lp64_binary128_slot(fn, ld_base,
                                    block->terminator.return_value, &src) ||
                    !emit_sp_mem(out, "ld.d", "$a0", src) ||
                    !emit_sp_mem(out, "ld.d", "$a1", src + 8U))
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                                               reject_stage, reject_block,
                                               reject_ins, __LINE__);
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
                fputs("  ret\n", out) == EOF) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_INDIRECT_BRANCH:
            if (!emit_sp_mem(out, "ld.d", "$t0",
                             (size_t)block->terminator.indirect_target * 8U) ||
                fputs("  jirl $zero, $t0, 0\n", out) == EOF)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                    reject_stage, reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_UNREACHABLE:
            /* Preserve Core CFG semantics: trap instead of falling through. */
            if (fputs("  break 0\n", out) == EOF) return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        case MINIC_CORE_TERMINATOR_BRANCH:
            if (fprintf(out, "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.branch_target) < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
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
                    return minic_multi_direct_machine_reject(fn, "loongarch64", name,
                        reject_stage, reject_block, reject_ins, __LINE__);
                break;
            }
            if (!emit_sp_mem(out, wide ? "ld.d" : "ld.w", "$t0",
                             (size_t)cond * 8U) ||
                fprintf(out,
                        "  bnez $t0, .Lmini_loong_f%zu_b%u\n"
                        "  b .Lmini_loong_f%zu_b%u\n",
                        id, (unsigned int)block->terminator.conditional.when_true,
                        id, (unsigned int)block->terminator.conditional.when_false) < 0)
                return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
            break;
        }
        default: return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
        }
    }
    reject_stage = "function_finish";
    reject_block = SIZE_MAX;
    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0 ||
        (decl->is_constructor &&
         fprintf(out, ".section .init_array,\"aw\",@init_array\n"
                      ".p2align 3\n  .quad %s\n", name) < 0))
        return minic_multi_direct_machine_reject(fn, "loongarch64", name, reject_stage,
                                   reject_block, reject_ins, __LINE__);
    /* ELF initializers are relocation entries in .init_array, not
     * executable calls in the current Core block. Mirror x86's existing
     * constructor ABI and let the dynamic loader execute before main. */
    return true;
}

#undef emit_sp_mem

bool minic_loongarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectProgramConfig config = {
        MINIC_TARGET_ARCH_LOONGARCH64,
        "loongarch64",
        "LoongArch64",
        ".section .note.GNU-stack,\"\",@progbits\n",
        emit_function,
        NULL
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
