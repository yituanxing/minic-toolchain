#include "target/aarch64/codegen.h"
#include "target/aarch64/machine_core_select.h"
#include "target/aarch64/machine_opcode.h"
#include "target/aarch64/abi.h"
#include "target/aarch64/asm_contract.h"
#include "target/common/direct_function_emit.h"
#include "target/common/direct_program_emit.h"
#include <stdio.h>

/* AAPCS64's physical placement is a target-owned value. Shared planners
 * consume this policy without selecting the machine by an architecture enum. */
static const MinicDirectLP64AbiPolicy aarch64_abi_policy = {
    .name = "aapcs64",
    .gp_argument_registers = 8U,
    .fp_argument_registers = 8U,
    .stack_slot_bytes = 8U,
    .stack_alignment = 16U,
    .indirect_result_argument_registers = 0U, /* dedicated x8 */
    .record_registers = MINIC_DIRECT_LP64_RECORD_WHOLE_OR_STACK,
    .wide_registers = MINIC_DIRECT_LP64_WIDE_EVEN_PAIR_OR_STACK,
    .long_double_in_fp_register = true,
    .variadic_floating_in_fp_register = true,
    .variadic_named_double_supported = true,
    .variadic_cursor = MINIC_DIRECT_LP64_VARIADIC_ABI_STATE,
    .variadic_save_bytes = 224U, /* 32B tag + 64B GPR + 128B FPR */
    .variadic_save_alignment = 16U,
    .extra_variadic_cursor_bytes = 32U,
    .variadic_requires_public_stack_alignment = false,
    .core_indirect_record = minic_aarch64_indirect_record_core,
    .core_indirect_return = minic_aarch64_indirect_record_core
};


static bool aarch64_emit_function(
    FILE *out, const MinicTargetInfo *target, const MinicCoreModule *module,
    const MinicCoreModuleFunctionSymbol *module_decl,
    const MinicCoreFunction *fn, const MinicCoreFunctionFacts *facts,
    const MinicCoreObjectStoragePlan *storage, size_t id,
    bool function_sections) {
    static const MinicDirectLP64FunctionEmitter emitter = {
        .architecture = "aarch64",
        .abi = &aarch64_abi_policy,
        .text_section_type = "%progbits",
        .elf_function_type = "%function",
        .init_array_type = "%init_array",
        .init_word_directive = "xword",
        .qualify_opaque_asm = minic_aarch64_qualified_opaque_asm,
        .select_machine = minic_aarch64_machine_select_core_subset,
        .initialize_machine_asm_policy =
            minic_aarch64_machine_gnu_asm_policy_initialize,
        .prove_selected_source_asm =
            minic_aarch64_machine_prove_selected_source_asm
    };
    return minic_multi_direct_lp64_emit_function(
        out, target, module, module_decl, fn, facts, storage,
        id, function_sections, &emitter);
}

bool minic_aarch64_write_core_module(
    const char *path,
    const MinicTargetInfo *target, const MinicCoreFunction *core_functions,
    size_t core_function_count, bool function_sections,
    const MinicCoreModule *module, MinicDiagnostic *diagnostic) {
    static const MinicMultiDirectModuleConfig config = {
        MINIC_TARGET_ARCH_AARCH64, "aarch64", "AArch64",
        ".section .note.GNU-stack,\"\",%progbits\n",
        "%function", "%object",
        aarch64_emit_function
    };
    return minic_multi_direct_write_module(
        path, target, core_functions, core_function_count,
        function_sections, module, &config, diagnostic);
}
