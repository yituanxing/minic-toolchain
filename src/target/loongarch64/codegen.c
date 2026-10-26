#include "target/loongarch64/codegen.h"
#include "target/loongarch64/block_emit.h"
#include "target/common/global_emit.h"
#include "target/common/direct_program_emit.h"
#include "target/common/direct_preflight.h"
#include "target/common/direct_function_plan.h"
#include "target/common/direct_call_preflight.h"
#include "target/common/direct_call_plan.h"
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


/* Macro scoped to emit_function: keep all existing fixed Core frame
 * accesses explicit, but distinguish them from outgoing SP slots. */
#define emit_sp_mem(_out, _op, _reg, _offset) \
    minic_loongarch64_emit_frame_mem((_out), (_op), (_reg), (_offset), dynamic_frame)

static bool emit_function_with_plan(FILE *out,
                          const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections,
                          const MinicDirectLP64FunctionPlan *plan) {
    const char *name = minic_c0_function_symbol_name(decl);
    const size_t frame = plan->frame_size;
    const size_t save_offset = plan->save_offset;
    const size_t va_count = plan->variadic_count;
    const size_t va_gp = plan->variadic_gp_count;
    const bool has_direct_calls = plan->has_calls;
    const bool dynamic_frame = plan->has_dynamic_stack;
    const char *reject_stage = "frame_finalization";
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX, i;
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
    {
        const MinicDirectLP64BlockContext blocks = {
            .program = program, .target = target, .fn = fn,
            .frame_plan = &plan->frame_plan, .incoming_plan = &plan->incoming_plan,
            .outgoing_calls = &plan->outgoing_calls,
            .name = name, .id = id, .frame = frame,
            .save_offset = save_offset, .va_gp = va_gp,
            .va_count = va_count, .has_direct_calls = has_direct_calls,
            .dynamic_frame = dynamic_frame
        };
        if (!minic_loongarch64_emit_core_blocks(out, &blocks)) return false;
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

/* Keep machine prologue, frame register and varargs saves LoongArch-owned.
 * All common preflight/ABI/layout work is performed once in a shared plan. */
static bool emit_function(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicFunction *decl,
                          const MinicCoreFunction *fn,
                          const MinicCoreFunctionFacts *facts, size_t id,
                          bool function_sections) {
    MinicDirectLP64FunctionPlan plan = {0};
    const char *name = minic_c0_function_symbol_name(decl);
    bool success;
    if (!minic_multi_direct_lp64_function_plan_prepare(
            program, target, decl, fn, facts, "loongarch64",
            MINIC_LP64_ABI_LOONGARCH64_LP64D,
            minic_multi_indirect_record, &plan)) {
        (void)minic_multi_direct_machine_reject(fn, "loongarch64", name,
            plan.failed_stage, plan.failed_block,
            plan.failed_instruction, __LINE__);
        success = false;
    } else {
        success = emit_function_with_plan(
            out, program, target, decl, fn, id, function_sections, &plan);
    }
    minic_multi_direct_lp64_function_plan_destroy(&plan);
    return success;
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
