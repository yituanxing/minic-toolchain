#ifndef MINIC_TARGET_COMMON_DIRECT_FUNCTION_EMIT_H
#define MINIC_TARGET_COMMON_DIRECT_FUNCTION_EMIT_H

#include "target/common/direct_function_plan.h"
#include "target/common/direct_block_context.h"
#include "target/common/direct_program_emit.h"
#include "target/common/module_global_emit.h"
#include "target/common/machine_core_coverage.h"
#include "target/common/machine_gnu_asm.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* The direct LP64 frontend-to-native orchestration is shared once:
 * ELF metadata, verified Common-to-ABI planning, complete BlockContext,
 * cleanup/rollback and diagnostics. ABI-specific prologue, machine blocks,
 * and epilogue stay in native hooks. Typed frame references own SP offsets. */
typedef struct MinicDirectLP64FunctionEmitter {
    const char *architecture;
    const MinicDirectLP64AbiPolicy *abi;
    const char *text_section_type;  /* @progbits or %progbits */
    const char *elf_function_type;  /* @function or %function */
    const char *init_array_type;    /* @init_array or %init_array */
    const char *init_word_directive; /* .quad or .xword */
    bool (*emit_prologue)(FILE *, const MinicDirectLP64BlockContext *);
    bool (*emit_core_blocks)(FILE *, const MinicDirectLP64BlockContext *);
    /* The ISA itself decides which operand-free hardware ASM is safe to
     * admit. Common merely validates one-to-one Core effect ownership. */
    MinicDirectLP64OpaqueAsmQualifier qualify_opaque_asm;
    /* A migrated target selects the ENTIRE function before any ELF text is
     * written. Old direct callbacks must then be NULL: no instruction-level
     * dual dispatch or source text stored as an MIR opcode is permitted. */
    bool (*select_machine)(MinicMachineFunction *,
                           const MinicDirectLP64BlockContext *);
    bool (*initialize_machine_asm_policy)(const MinicTargetInfo *,
                                          MinicMachineGNUAsmPolicy *);
} MinicDirectLP64FunctionEmitter;

static inline bool minic_multi_direct_lp64_emit_function(
    FILE *out, const MinicTargetInfo *target,
    const MinicCoreModule *module,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn,
    const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage,
    size_t id, bool function_sections,
    const MinicDirectLP64FunctionEmitter *config) {
    MinicDirectLP64FunctionPlan plan = {0};
    MinicDirectLP64BlockContext ctx = {0};
    MinicMachineFunction machine = {0};
    MinicMachineGNUAsmPolicy machine_policy = {0};
    const char *name;
    const char *section;
    size_t alignment;
    bool internal, weak, constructor;
    bool use_machine;
    bool ok = false;

    if (config == NULL || config->architecture == NULL ||
        !minic_direct_lp64_abi_policy_valid(config->abi) ||
        config->abi->core_indirect_record == NULL ||
        config->abi->core_indirect_return == NULL ||
        config->text_section_type == NULL ||
        config->elf_function_type == NULL || config->init_array_type == NULL ||
        config->init_word_directive == NULL || out == NULL || module == NULL ||
        module_decl == NULL)
        return false;
    use_machine = config->select_machine != NULL;
    if (use_machine) {
        if (config->emit_prologue != NULL ||
            config->emit_core_blocks != NULL ||
            config->initialize_machine_asm_policy == NULL)
            return false;
    } else if (config->emit_prologue == NULL ||
               config->emit_core_blocks == NULL ||
               config->initialize_machine_asm_policy != NULL) {
        return false;
    }
    name = module_decl->name;
    section = module_decl->section_name;
    alignment = module_decl->explicit_alignment;
    internal = module_decl->is_internal;
    weak = module_decl->is_weak;
    constructor = module_decl->is_constructor;
    if (name == NULL || name[0] == '\0') return false;
    if (!minic_multi_direct_lp64_function_plan_prepare(
            module_decl, fn, facts, storage, config->architecture,
            config->abi, config->qualify_opaque_asm,
            &plan)) {
        (void)minic_multi_direct_machine_reject(fn, config->architecture,
            name, plan.failed_stage, plan.failed_block,
            plan.failed_instruction, __LINE__);
        goto cleanup;
    }

    ctx = (MinicDirectLP64BlockContext){
        .module = module, .target = target, .fn = fn, .core_facts = facts,
        .frame_plan = &plan.frame_plan,
        .machine_frame = &plan.machine_frame,
        .incoming_plan = &plan.incoming_plan,
        .outgoing_calls = &plan.outgoing_calls,
        .name = name, .id = id
    };
    if (use_machine) {
        if (!config->select_machine(&machine, &ctx) ||
            machine.target != target ||
            machine.architecture != target->architecture ||
            machine.name == NULL || strcmp(machine.name, name) != 0 ||
            machine.calling_convention !=
                minic_core_function_calling_convention(fn) ||
            machine.frame_size != plan.machine_frame.frame_size ||
            machine.frame_alignment != plan.machine_frame.frame_alignment ||
            machine.has_dynamic_stack !=
                plan.machine_frame.has_dynamic_stack ||
            !minic_machine_core_coverage_verify(
                fn, facts->reachable_blocks, &machine) ||
            !config->initialize_machine_asm_policy(target, &machine_policy) ||
            !minic_machine_gnu_asm_preflight(&machine, &machine_policy)) {
            (void)minic_multi_direct_machine_reject(fn,
                config->architecture, name, "machine_function_preflight",
                SIZE_MAX, SIZE_MAX, __LINE__);
            goto cleanup;
        }
    }

    /* A GNU explicit section overrides compiler-generated function sections. */
    if (section != NULL) {
        if (fprintf(out, ".section %s,\"ax\",%s\n",
                    section, config->text_section_type) < 0)
            goto output_failed;
    } else if (function_sections) {
        if (fprintf(out, ".section .text.%s,\"ax\",%s\n",
                    name, config->text_section_type) < 0) goto output_failed;
    } else if (fputs(".text\n", out) == EOF) goto output_failed;

    if ((!internal &&
         fprintf(out, weak ? ".weak %s\n" : ".globl %s\n", name) < 0) ||
        (!internal &&
         !minic_multi_module_emit_elf_symbol_visibility(
             out, module_decl->visibility, name)) ||
        fprintf(out, ".balign %zu\n.type %s,%s\n%s:\n",
                alignment > 4U ? alignment : 4U,
                name, config->elf_function_type, name) < 0)
        goto output_failed;

    if (use_machine) {
        if (!minic_machine_gnu_asm_emit_body(out, &machine, &machine_policy))
            goto cleanup;
    } else {
        if (!config->emit_prologue(out, &ctx)) goto cleanup;
        if (!config->emit_core_blocks(out, &ctx)) goto cleanup;
    }

    if (fprintf(out, ".size %s,.-%s\n", name, name) < 0 ||
        (constructor &&
         fprintf(out,
                 ".section .init_array,\"aw\",%s\n.p2align 3\n  .%s %s\n",
                 config->init_array_type, config->init_word_directive, name) < 0))
        goto output_finish_failed;

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
    minic_machine_function_destroy(&machine);
    minic_multi_direct_lp64_function_plan_destroy(&plan);
    return ok;
}
#endif
