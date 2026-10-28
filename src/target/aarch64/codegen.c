#include "target/aarch64/codegen.h"
#include "target/aarch64/block_emit.h"
#include "target/aarch64/abi.h"
#include "target/aarch64/asm_legalize.h"
#include "target/aarch64/asm_contract.h"
#include "target/common/direct_function_emit.h"
#include "target/common/direct_program_emit.h"
#include <stdint.h>
#include <stdio.h>

/* Only native AAPCS64 prologue policy lives here. Symbol metadata,
 * Core-to-ABI planning, dynamic rewrite lifecycle, BlockContext ownership
 * and ELF completion are shared with the other direct LP64 backend. */
static bool aarch64_emit_prologue(
    FILE *out, const MinicDirectLP64BlockContext *ctx) {
    const MinicCoreFunction *fn = ctx->fn;
    const char *name = ctx->name;
    const size_t frame = ctx->frame;
    const size_t save_offset = ctx->save_offset;
    const bool has_calls = ctx->has_direct_calls;
    const bool dynamic_frame = ctx->dynamic_frame;
    size_t i;
    if (frame != 0U &&
        (fprintf(out, "  movz x12, #%zu\n", frame & 65535U) < 0 ||
         (frame > 65535U &&
          fprintf(out, "  movk x12, #%zu, lsl #16\n", frame >> 16U) < 0) ||
         fputs("  sub sp, sp, x12\n", out) == EOF))
        goto fail;
    if (has_calls && fprintf(out, "  str x30, [sp, #%zu]\n",
                             save_offset) < 0)
        goto fail;
    /* x19 is the AAPCS64 callee-saved stable frame anchor when alloca
     * moves sp. Call arguments still use the live outgoing sp area. */
    if (dynamic_frame &&
        (fprintf(out, "  str x19, [sp, #%zu]\n",
                 save_offset + (has_calls ? 8U : 0U)) < 0 ||
         fputs("  mov x19, sp\n", out) == EOF))
        goto fail;
    if (ctx->va_count != 0U) {
        for (i = 0U; i < 8U; ++i) {
            if (fprintf(out, "  str x%zu, [sp, #%zu]\n",
                        i, ctx->va_base + 32U + i * 8U) < 0 ||
                fprintf(out, "  str q%zu, [sp, #%zu]\n",
                        i, ctx->va_base + 96U + i * 16U) < 0)
                return minic_multi_direct_machine_reject(
                    fn, "aarch64", name, "variadic_save", SIZE_MAX,
                    SIZE_MAX, __LINE__);
        }
    }
    if (ctx->has_indirect_return) {
        const size_t slot = ctx->indirect_return_offset;
        /* AAPCS64 x8 carries the caller-owned result buffer. It is not
         * call-preserved: save it in our own frame before any nested BL. */
        if (fprintf(out, "  movz x11, #%zu\n", slot & 65535U) < 0 ||
            (slot > 65535U &&
             fprintf(out, "  movk x11, #%zu, lsl #16\n", slot >> 16U) < 0) ||
            fputs("  add x11, sp, x11\n  str x8, [x11]\n", out) == EOF)
            goto fail;
    }
    return true;
fail:
    return minic_multi_direct_machine_reject(
        fn, "aarch64", name, "frame_finalization", SIZE_MAX,
        SIZE_MAX, __LINE__);
}

static bool aarch64_emit_function(
    FILE *out, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicFunction *decl,
    const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t id,
    bool function_sections) {
    static const MinicDirectLP64FunctionEmitter emitter = {
        "aarch64", MINIC_LP64_ABI_AAPCS64,
        minic_aarch64_indirect_record, minic_aarch64_indirect_record,
        "%progbits", "%function", "%init_array", "xword",
        aarch64_emit_prologue, minic_aarch64_emit_core_blocks,
        minic_aarch64_copy_dynamic_frame_asm,
        minic_aarch64_qualified_opaque_asm
    };
    return minic_multi_direct_lp64_emit_function(
        out, program, target, decl, fn, facts, storage,
        id, function_sections, &emitter);
}

bool minic_aarch64_write_c0_program_with_core_functions(
    const char *path, const MinicC0Program *program,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectProgramConfig config = {
        MINIC_TARGET_ARCH_AARCH64, "aarch64", "AArch64",
        ".section .note.GNU-stack,\"\",%progbits\n",
        "%function", "%object",
        aarch64_emit_function, minic_aarch64_legalize_large_sp_memory
    };
    return minic_multi_direct_write_program(
        path, program, target, core_functions, core_function_count,
        function_sections, &config, diagnostic);
}
