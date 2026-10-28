#include "target/loongarch64/codegen.h"
#include "target/loongarch64/block_emit.h"
#include "target/loongarch64/frame_emit.h"
#include "target/loongarch64/asm_contract.h"
#include "target/common/direct_function_emit.h"
#include "target/common/lp64_aggregate.h"
#include "target/common/direct_program_emit.h"
#include <stdint.h>
#include <stdio.h>

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
    if (!minic_loongarch64_emit_sp_adjust(out, frame, false) ||
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
                    frame - 64U + i * 8U, dynamic_frame))
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
    static const MinicDirectLP64FunctionEmitter emitter = {
        "loongarch64", MINIC_LP64_ABI_LOONGARCH64_LP64D,
        minic_multi_indirect_record, minic_multi_indirect_record,
        "@progbits", "@function", "@init_array", "quad",
        loongarch64_emit_prologue, minic_loongarch64_emit_core_blocks,
        NULL, minic_loongarch64_qualified_opaque_asm
    };
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
        loongarch64_emit_function, NULL
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
