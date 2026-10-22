#include "target/aarch64/codegen.h"
#include "target/aarch64/block_emit.h"
#include "target/common/global_emit.h"
#include "target/common/direct_program_emit.h"
#include "target/common/direct_preflight.h"
#include "target/common/direct_function_plan.h"
#include "target/common/direct_call_preflight.h"
#include "target/common/direct_call_plan.h"
#include "target/common/lp64_scalar.h"
#include "target/common/lp64_frame.h"
#include "target/common/lp64_aggregate.h"
#include "target/aarch64/abi.h"
#include "target/aarch64/asm_legalize.h"

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


static bool emit_function_body_with_plan(FILE *out,
                               const MinicC0Program *program,
                               const MinicTargetInfo *target,
                               const MinicFunction *decl,
                               const MinicCoreFunction *fn, size_t id,
                               bool function_sections,
                               const MinicDirectLP64FunctionPlan *plan) {
    const char *name = minic_c0_function_symbol_name(decl);
    const size_t frame = plan->frame_size;
    const size_t save_offset = plan->save_offset;
    const size_t va_base = plan->variadic_save_base;
    const size_t va_count = plan->variadic_count;
    const size_t va_gp = plan->variadic_gp_count;
    const size_t va_fp = plan->variadic_fp_count;
    const bool has_direct_calls = plan->has_calls;
    const bool dynamic_frame = plan->has_dynamic_stack;
    const char *reject_stage = "frame_finalization";
    size_t reject_block = SIZE_MAX, reject_ins = SIZE_MAX, i;
    if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",%%progbits\n", name) < 0)
            return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    } else if (fprintf(out, ".text\n") < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal && fprintf(out, ".globl %s\n", name) < 0)
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (!decl->is_internal &&
        !minic_multi_emit_elf_symbol_visibility(out, decl->visibility, name))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (fprintf(out, ".p2align 2\n.type %s,%%function\n%s:\n", name, name) < 0)
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    /* No illegal AArch64 add/sub immediates on real >4KiB frames. */
    if (frame != 0U &&
        (fprintf(out, "  movz x12, #%zu\n", frame & 65535U) < 0 ||
         (frame > 65535U &&
          fprintf(out, "  movk x12, #%zu, lsl #16\n", frame >> 16U) < 0) ||
         fputs("  sub sp, sp, x12\n", out) == EOF))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    if (has_direct_calls &&
        fprintf(out, "  str x30, [sp, #%zu]\n",
                save_offset) < 0) return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                    reject_block, reject_ins, __LINE__);
    /* x19 is the AAPCS64 callee-saved fixed-frame anchor. A dynamic
     * alloca changes SP, but SSA slots and incoming parameters do not
     * change position. The outgoing call spill area still uses raw SP. */
    if (dynamic_frame &&
        (fprintf(out, "  str x19, [sp, #%zu]\n",
                 save_offset + (has_direct_calls ? 8U : 0U)) < 0 ||
         fputs("  mov x19, sp\n", out) == EOF))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                   reject_block, reject_ins, __LINE__);
    if (va_count != 0U) {
        /* Preserve incoming anonymous gp/fp arguments before any call. */
        for (i = 0U; i < 8U; ++i)
            if (fprintf(out, "  str x%zu, [sp, #%zu]\n",
                        i, va_base + 32U + i * 8U) < 0 ||
                fprintf(out, "  str q%zu, [sp, #%zu]\n",
                        i, va_base + 96U + i * 16U) < 0)
                return minic_multi_direct_machine_reject(fn, "aarch64", name,
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
            .va_base = va_base, .va_fp = va_fp,
            .va_count = va_count, .has_direct_calls = has_direct_calls,
            .dynamic_frame = dynamic_frame
        };
        if (!minic_aarch64_emit_core_blocks(out, &blocks)) return false;
    }
    reject_stage = "function_finish";
    reject_block = SIZE_MAX;
    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0 ||
        (decl->is_constructor &&
         fprintf(out, ".section .init_array,\"aw\",%%init_array\n"
                      ".p2align 3\n  .xword %s\n", name) < 0))
        return minic_multi_direct_machine_reject(fn, "aarch64", name, reject_stage,
                                   reject_block, reject_ins, __LINE__);
    /* ELF initializers are relocation entries in .init_array, not
     * executable calls in the current Core block. Mirror x86's existing
     * constructor ABI and let the dynamic loader execute before main. */
    return true;
}

/* Construct one complete call/frame plan and reuse it for machine emission.
 * AArch64 dynamic-SP text legalization sees the *same* analyzed function,
 * rather than running a separate Core instruction scan to rediscover alloca. */
static bool emit_function(FILE *out, const MinicC0Program *program,
                          const MinicTargetInfo *target,
                          const MinicFunction *decl,
                          const MinicCoreFunction *fn, size_t id,
                          bool function_sections) {
    MinicDirectLP64FunctionPlan plan = {0};
    const char *name = minic_c0_function_symbol_name(decl);
    bool success = false;
    FILE *temporary = NULL;
    if (!minic_multi_direct_lp64_function_plan_prepare(
            program, target, decl, fn, "aarch64",
            MINIC_LP64_ABI_AAPCS64, minic_aarch64_indirect_record, &plan)) {
        (void)minic_multi_direct_machine_reject(fn, "aarch64", name,
            plan.failed_stage, plan.failed_block,
            plan.failed_instruction, __LINE__);
        goto done;
    }
    if (!plan.has_dynamic_stack) {
        success = emit_function_body_with_plan(
            out, program, target, decl, fn, id, function_sections, &plan);
        goto done;
    }
    temporary = tmpfile();
    if (temporary == NULL)
        goto done;
    success = emit_function_body_with_plan(
        temporary, program, target, decl, fn, id, function_sections, &plan);
    if (success && (fflush(temporary) != 0 ||
                    fseek(temporary, 0L, SEEK_SET) != 0 ||
                    !minic_aarch64_copy_dynamic_frame_asm(out, temporary)))
        success = false;
done:
    if (temporary != NULL && fclose(temporary) != 0)
        success = false;
    minic_multi_direct_lp64_function_plan_destroy(&plan);
    return success;
}

bool minic_aarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectProgramConfig config = {
        MINIC_TARGET_ARCH_AARCH64,
        "aarch64",
        "AArch64",
        ".section .note.GNU-stack,\"\",%progbits\n",
        emit_function,
        minic_aarch64_legalize_large_sp_memory
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
