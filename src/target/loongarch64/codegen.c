#include "target/loongarch64/codegen.h"
#include "target/loongarch64/block_emit.h"
#include "target/loongarch64/frame_emit.h"
#include "target/loongarch64/asm_contract.h"
#include "target/common/direct_function_emit.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/direct_program_emit.h"
#include <stdint.h>
#include <stdio.h>

/* LoongArch LP64D is not AAPCS64 with renamed registers: notably the first
 * GAR holds an indirect result, aggregate pairs may split, and va_list is
 * a contiguous linear cursor. Keep those choices alongside machine emission. */
static const MinicDirectLP64AbiPolicy loongarch64_abi_policy = {
    .name = "loongarch64-lp64d",
    .gp_argument_registers = 8U,
    .fp_argument_registers = 8U,
    .stack_slot_bytes = 8U,
    .stack_alignment = 16U,
    .indirect_result_argument_registers = 1U,
    .record_registers = MINIC_DIRECT_LP64_RECORD_SPLIT_WITH_STACK,
    .wide_registers = MINIC_DIRECT_LP64_WIDE_SPLIT_WITH_STACK,
    .long_double_in_fp_register = false,
    .variadic_floating_in_fp_register = false,
    .variadic_named_double_supported = false,
    .variadic_align_wide_gp_pair = true,
    .variadic_cursor = MINIC_DIRECT_LP64_VARIADIC_LINEAR_GP,
    .variadic_save_bytes = 64U,
    .variadic_save_alignment = 8U,
    .extra_variadic_cursor_bytes = 0U,
    .variadic_requires_public_stack_alignment = true
};

static bool loongarch64_executed_fp_type(MinicType type) {
    return minic_type_is_float(type) || minic_type_is_double(type) ||
           minic_type_is_long_double(type) || minic_type_is_float128(type);
}

/* LP64S passes floating arguments in GPRs and forbids compiler-generated FPU
 * instructions in the kernel. The integer ABI remains compatible with LP64D,
 * but we must not miscompile a future float operation or FP-valued call while
 * soft-float arithmetic/return lowering is still unavailable. Declarations
 * alone (including external FP prototypes and float-pointer types) are fine. */
static bool loongarch64_lp64s_executable_supported(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn) {
    size_t i;
    size_t count, width, j;
    if (fn == NULL || program == NULL || target == NULL ||
        loongarch64_executed_fp_type(fn->return_type)) return false;
    if (minic_type_is_record(fn->return_type) &&
        minic_multi_fp_return_record(program, target, fn->return_type,
                                     &count, &width)) return false;
    for (i = 0U; i < fn->parameter_count; ++i)
        if (loongarch64_executed_fp_type(fn->parameter_types[i])) return false;
    /* Validate actual Core call signatures, not every source declaration.
     * An FP formal type can exist even when argument conversion was folded
     * or no FP-typed SSA operand survives in the call lowering. */
    for (i = 0U; i < fn->callee_count; ++i) {
        const MinicCoreCallee *call = &fn->callees[i];
        if (loongarch64_executed_fp_type(call->return_type)) return false;
        for (j = 0U; j < call->parameter_count; ++j)
            if (loongarch64_executed_fp_type(call->parameter_types[j]))
                return false;
    }
    for (i = 0U; i < fn->call_signature_count; ++i) {
        const MinicCoreCallSignature *call = &fn->call_signatures[i];
        if (loongarch64_executed_fp_type(call->return_type)) return false;
        for (j = 0U; j < call->parameter_count; ++j)
            if (loongarch64_executed_fp_type(call->parameter_types[j]))
                return false;
    }
    for (i = 0U; i < fn->value_count; ++i)
        if (loongarch64_executed_fp_type(fn->values[i].type)) return false;
    for (i = 0U; i < fn->instruction_count; ++i) {
        const MinicCoreInstruction *ins = &fn->instructions[i];
        if (loongarch64_executed_fp_type(ins->type)) return false;
        if ((ins->kind == MINIC_CORE_INSTRUCTION_CALL ||
             ins->kind == MINIC_CORE_INSTRUCTION_INDIRECT_CALL) &&
            minic_type_is_record(ins->type) &&
            minic_multi_fp_return_record(program, target, ins->type,
                                         &count, &width)) return false;
    }
    return true;
}

/* LoongArch64 owns every LP64D frame/ABI register action. Common code owns
 * the function envelope, checked planning, cleanup and ELF serialization. */
static bool loongarch64_emit_prologue(
    FILE *out, const MinicDirectLP64BlockContext *ctx) {
    static const char *const registers[8] = {
        "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7"
    };
    const MinicCoreFunction *fn = ctx->fn;
    const char *name = ctx->name;
    const size_t frame = ctx->frame;
    const size_t save_offset = ctx->save_offset;
    const bool has_calls = ctx->has_direct_calls;
    const bool dynamic_frame = ctx->dynamic_frame;
    size_t i;
    if (ctx->frame_alignment > 16U) {
        /* Entry $sp is only 16-aligned. Reserve worst-case alignment slack,
         * preserve the exact entry $sp, and round a stable callee-saved $s0
         * frame base UP. All object slots are aligned relative to $s0;
         * outgoing call stack space continues to use the 16-aligned $sp. */
        if (!dynamic_frame || ctx->saved_entry_sp_offset == SIZE_MAX ||
            fputs("  move $t0, $sp\n", out) == EOF ||
            !minic_loongarch64_emit_sp_adjust(out, frame, false) ||
            fprintf(out, "  move $t1, $s0\n"
                         "  addi.d $s0, $sp, %zu\n"
                         "  li.d $t3, -%zu\n"
                         "  and $s0, $s0, $t3\n",
                    ctx->frame_alignment - 1U,
                    ctx->frame_alignment) < 0 ||
            !minic_loongarch64_emit_frame_mem(
                out, "st.d", "$t0", ctx->saved_entry_sp_offset, true) ||
            !minic_loongarch64_emit_frame_mem(
                out, "st.d", "$t1",
                save_offset + (has_calls ? 8U : 0U), true))
            goto fail;
    } else if (!minic_loongarch64_emit_sp_adjust(out, frame, false) ||
               (dynamic_frame &&
                (!minic_loongarch64_emit_frame_mem(
                     out, "st.d", "$s0",
                     save_offset + (has_calls ? 8U : 0U), false) ||
                 fputs("  move $s0, $sp\n", out) == EOF)))
        goto fail;
    if (has_calls &&
        !minic_loongarch64_emit_frame_mem(
            out, "st.d", "$ra", save_offset, dynamic_frame))
        goto fail;
    if (ctx->has_indirect_return) {
        /* LP64D uses $a0 as a hidden first GAR, unlike AAPCS64 x8.
         * A nested BL may destroy $a0, so pin the caller-owned buffer
         * address in our stable local frame before any normal instruction. */
        if (!minic_loongarch64_emit_frame_mem(
                out, "st.d", "$a0", ctx->indirect_return_offset,
                dynamic_frame))
            goto fail;
    }
    if (ctx->va_count != 0U) {
        for (i = 0U; i < 8U; ++i)
            if (!minic_loongarch64_emit_frame_mem(
                    out, "st.d", registers[i],
                    ctx->va_base + i * 8U, dynamic_frame))
                return minic_multi_direct_machine_reject(
                    fn, "loongarch64", name, "variadic_save",
                    SIZE_MAX, SIZE_MAX, __LINE__);
    }
    return true;
fail:
    return minic_multi_direct_machine_reject(
        fn, "loongarch64", name, "frame_finalization",
        SIZE_MAX, SIZE_MAX, __LINE__);
}

static bool loongarch64_emit_function(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicFunction *decl,
    const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t id,
    bool function_sections) {
    MinicDirectLP64AbiPolicy abi = loongarch64_abi_policy;
    const MinicDirectLP64FunctionEmitter emitter = {
        "loongarch64", &abi,
        minic_multi_indirect_record, minic_multi_indirect_record,
        "@progbits", "@function", "@init_array", "quad",
        loongarch64_emit_prologue, minic_loongarch64_emit_core_blocks,
        minic_loongarch64_qualified_opaque_asm
    };
    if (target == NULL || fn == NULL) return false;
    if (target->loongarch64_soft_float_abi) {
        if (!loongarch64_lp64s_executable_supported(program, target, fn))
            return minic_multi_direct_machine_reject(
                fn, "loongarch64", fn->name,
                "lp64s-float-codegen-not-supported", SIZE_MAX, SIZE_MAX,
                __LINE__);
        abi.name = "loongarch64-lp64s";
        abi.fp_argument_registers = 0U;
    }
    return minic_multi_direct_lp64_emit_function(
        out, program, target, decl, fn, facts, storage,
        id, function_sections, &emitter);
}

bool minic_loongarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectProgramConfig config = {
        MINIC_TARGET_ARCH_LOONGARCH64, "loongarch64", "LoongArch64",
        ".section .note.GNU-stack,\"\",@progbits\n",
        "@function", "@object",
        loongarch64_emit_function
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
