#ifndef MINIC_TARGET_COMMON_DIRECT_FUNCTION_EMIT_H
#define MINIC_TARGET_COMMON_DIRECT_FUNCTION_EMIT_H

#include "target/common/direct_function_plan.h"
#include "target/common/direct_block_context.h"
#include "target/common/direct_program_emit.h"
#include "target/common/global_emit.h"
#include <stdint.h>
#include <stdio.h>

/* The direct LP64 frontend-to-native orchestration is shared once:
 * ELF metadata, verified Common-to-ABI planning, complete BlockContext,
 * optional per-function dynamic-SP rewrite, cleanup/rollback and diagnostics.
 * ABI-specific prologue, machine blocks, and epilogue stay in native hooks. */
typedef struct MinicDirectLP64FunctionEmitter {
    const char *architecture;
    MinicLP64DirectABI abi;
    MinicDirectLP64IndirectRecord indirect_record;
    /* ABI-owned return classifier. NULL preserves the old fail-closed path. */
    MinicDirectLP64IndirectRecord indirect_return;
    const char *text_section_type;  /* @progbits or %progbits */
    const char *elf_function_type;  /* @function or %function */
    const char *init_array_type;    /* @init_array or %init_array */
    const char *init_word_directive; /* .quad or .xword */
    bool (*emit_prologue)(FILE *, const MinicDirectLP64BlockContext *);
    bool (*emit_core_blocks)(FILE *, const MinicDirectLP64BlockContext *);
    /* NULL unless the ISA requires a text rewrite after dynamic alloca. */
    bool (*rewrite_dynamic_asm)(FILE *dest, FILE *source);
    /* The ISA itself decides which operand-free hardware ASM is safe to
     * admit. Common merely validates one-to-one Core effect ownership. */
    MinicDirectLP64OpaqueAsmQualifier qualify_opaque_asm;
} MinicDirectLP64FunctionEmitter;

static inline bool minic_multi_direct_lp64_emit_function(
    FILE *out, const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicFunction *decl, const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage,
    size_t id, bool function_sections,
    const MinicDirectLP64FunctionEmitter *config) {
    MinicDirectLP64FunctionPlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    const char *name;
    FILE *emission = out, *staged = NULL;
    bool ok = false;

    if (config == NULL || config->architecture == NULL ||
        config->indirect_record == NULL || config->text_section_type == NULL ||
        config->elf_function_type == NULL || config->init_array_type == NULL ||
        config->init_word_directive == NULL || config->emit_prologue == NULL ||
        config->emit_core_blocks == NULL || out == NULL || decl == NULL)
        return false;
    name = minic_c0_function_symbol_name(decl);
    if (!minic_multi_direct_lp64_function_plan_prepare(
            program, target, decl, fn, facts, storage, config->architecture,
            config->abi, config->indirect_record, config->indirect_return,
            config->qualify_opaque_asm,
            &plan)) {
        (void)minic_multi_direct_machine_reject(fn, config->architecture,
            name, plan.failed_stage, plan.failed_block,
            plan.failed_instruction, __LINE__);
        goto cleanup;
    }

    if (plan.has_dynamic_stack && config->rewrite_dynamic_asm != NULL) {
        staged = tmpfile();
        if (staged == NULL) goto cleanup;
        emission = staged;
    }

    /* A GNU explicit section overrides compiler-generated function sections. */
    if (decl->section_name != NULL) {
        if (fprintf(emission, ".section %s,\"ax\",%s\n",
                    decl->section_name, config->text_section_type) < 0)
            goto output_failed;
    } else if (function_sections) {
        if (fprintf(emission, ".section .text.%s,\"ax\",%s\n",
                    name, config->text_section_type) < 0) goto output_failed;
    } else if (fputs(".text\n", emission) == EOF) goto output_failed;

    if ((!decl->is_internal &&
         fprintf(emission, decl->is_weak ? ".weak %s\n" : ".globl %s\n", name) < 0) ||
        (!decl->is_internal &&
         !minic_multi_emit_elf_symbol_visibility(
             emission, decl->visibility, name)) ||
        fprintf(emission, ".p2align 2\n.type %s,%s\n%s:\n",
                name, config->elf_function_type, name) < 0)
        goto output_failed;

    ctx = (MinicDirectLP64BlockContext){
        .program = program, .target = target, .fn = fn,
        .frame_plan = &plan.frame_plan,
        .incoming_plan = &plan.incoming_plan,
        .outgoing_calls = &plan.outgoing_calls,
        .name = name, .id = id, .frame = plan.frame_size,
        .save_offset = plan.save_offset,
        .indirect_return_offset = plan.indirect_return_offset,
        .va_base = plan.variadic_save_base,
        .va_gp = plan.variadic_gp_count,
        .va_fp = plan.variadic_fp_count,
        .va_count = plan.variadic_count,
        .has_direct_calls = plan.has_calls,
        .has_indirect_return = plan.has_indirect_return,
        .dynamic_frame = plan.has_dynamic_stack
    };
    if (!config->emit_prologue(emission, &ctx)) goto cleanup;
    if (!config->emit_core_blocks(emission, &ctx)) goto cleanup;

    if (fprintf(emission, ".size %s,.-%s\n", name, name) < 0 ||
        (decl->is_constructor &&
         fprintf(emission,
                 ".section .init_array,\"aw\",%s\n.p2align 3\n  .%s %s\n",
                 config->init_array_type, config->init_word_directive, name) < 0))
        goto output_finish_failed;

    if (staged != NULL &&
        (fflush(staged) != 0 || fseek(staged, 0L, SEEK_SET) != 0 ||
         !config->rewrite_dynamic_asm(out, staged)))
        goto cleanup;
    ok = true;
    goto cleanup;

output_failed:
    (void)minic_multi_direct_machine_reject(
        fn, config->architecture, name, "frame_finalization",
        SIZE_MAX, SIZE_MAX, __LINE__);
    goto cleanup;
output_finish_failed:
    (void)minic_multi_direct_machine_reject(
        fn, config->architecture, name, "function_finish",
        SIZE_MAX, SIZE_MAX, __LINE__);
cleanup:
    if (staged != NULL && fclose(staged) != 0)
        ok = false;
    minic_multi_direct_lp64_function_plan_destroy(&plan);
    return ok;
}
#endif
