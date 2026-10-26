#include "target/backend.h"

#include "target/riscv64/codegen.h"
#include "target/riscv32/rv32_emit.h"
#include "target/x86_64/codegen.h"
#include "target/x86_64/asm_contract.h"
#include "target/aarch64/codegen.h"
#include "target/loongarch64/codegen.h"
#include <string.h>

static bool emit_riscv64(const char *output_path, const MinicC0Program *program,
                        const MinicTargetInfo *target,
                        const MinicCoreFunction *functions,
                        size_t function_count, bool function_sections,
                        MinicDiagnostic *diagnostic) {
    return minic_riscv64_write_c0_program_with_core_functions(
        output_path, program, functions, function_count,
        function_sections, target,
        diagnostic);
}

/* Explicit selection avoids accidental target-number indexing and fallback.
 * More targets can register here without modifying compiler.c or Core IR. */
static const MinicBackend riscv64_backend = {
    MINIC_TARGET_ARCH_RISCV64, "riscv64", emit_riscv64, NULL, true
};
static const MinicBackend riscv32_backend = {
    MINIC_TARGET_ARCH_RISCV32, "riscv32", minic_riscv32_write_core_program, NULL, true
};
static const MinicBackend x86_64_backend = {
    MINIC_TARGET_ARCH_X86_64, "x86_64",
    minic_x86_64_write_c0_program_with_core_functions,
    minic_x86_64_qualify_inline_asm, false
};
static const MinicBackend aarch64_backend = {
    MINIC_TARGET_ARCH_AARCH64, "aarch64",
    minic_aarch64_write_c0_program_with_core_functions, NULL, false
};
static const MinicBackend loongarch64_backend = {
    MINIC_TARGET_ARCH_LOONGARCH64, "loongarch64",
    minic_loongarch64_write_c0_program_with_core_functions, NULL, false
};

const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target) {
    if (target == NULL) return NULL;
    switch (target->architecture) {
    case MINIC_TARGET_ARCH_RISCV64: return &riscv64_backend;
    case MINIC_TARGET_ARCH_RISCV32: return &riscv32_backend;
    case MINIC_TARGET_ARCH_X86_64: return &x86_64_backend;
    case MINIC_TARGET_ARCH_AARCH64: return &aarch64_backend;
    case MINIC_TARGET_ARCH_LOONGARCH64: return &loongarch64_backend;
    }
    return NULL;
}

bool minic_backend_qualify_inline_asm(
    const MinicBackend *backend, const MinicC0Program *program,
    const MinicInlineAsm *source, MinicTargetInlineAsmContract *contract) {
    if (!backend || !program || !source || !contract ||
        !backend->qualify_inline_asm) return false;
    memset(contract, 0, sizeof(*contract));
    return backend->qualify_inline_asm(program, source, contract) &&
           contract->operand_count == source->output_count + source->input_count &&
           contract->operand_count <= MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT;
}

bool minic_backend_emit_assembly(
    const MinicBackend *backend, const char *output_path,
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *functions, size_t function_count,
    bool function_sections, MinicDiagnostic *diagnostic) {
    if (backend == NULL || target == NULL ||
        backend->architecture != target->architecture ||
        backend->emit_program == NULL)
        return false;
    return backend->emit_program(output_path, program, target, functions,
                                 function_count, function_sections, diagnostic);
}
