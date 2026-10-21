#include "target/backend.h"

#include "target/riscv64/codegen.h"
#include "target/riscv64/layout.h"
#include "target/x86_64/codegen.h"
#include "target/aarch64/codegen.h"
#include "target/loongarch64/codegen.h"

/*
 * Adapt the mature RV64 backend without rewriting its validated ABI and
 * stack-lifetime machinery. Other targets already accept TargetInfo.
 */
static bool prepare_riscv64(const char *source_path, MinicC0Program *program,
                           const MinicTargetInfo *target,
                           MinicDiagnostic *diagnostic) {
    (void)target;
    return minic_riscv64_layout_program(source_path, program, diagnostic);
}

static bool emit_riscv64(const char *output_path, const MinicC0Program *program,
                        const MinicTargetInfo *target,
                        const MinicCoreFunction *functions,
                        size_t function_count, bool function_sections,
                        MinicDiagnostic *diagnostic) {
    (void)target;
    return minic_riscv64_write_c0_program_with_core_functions(
        output_path, program, functions, function_count,
        function_sections, diagnostic);
}

/* Explicit selection avoids accidental target-number indexing and fallback.
 * More targets can register here without modifying compiler.c or Core IR. */
static const MinicBackend riscv64_backend = {
    MINIC_TARGET_ARCH_RISCV64, "riscv64", prepare_riscv64, emit_riscv64
};
static const MinicBackend x86_64_backend = {
    MINIC_TARGET_ARCH_X86_64, "x86_64", NULL,
    minic_x86_64_write_c0_program_with_core_functions
};
static const MinicBackend aarch64_backend = {
    MINIC_TARGET_ARCH_AARCH64, "aarch64", NULL,
    minic_aarch64_write_c0_program_with_core_functions
};
static const MinicBackend loongarch64_backend = {
    MINIC_TARGET_ARCH_LOONGARCH64, "loongarch64", NULL,
    minic_loongarch64_write_c0_program_with_core_functions
};

const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target) {
    if (target == NULL) return NULL;
    switch (target->architecture) {
    case MINIC_TARGET_ARCH_RISCV64: return &riscv64_backend;
    case MINIC_TARGET_ARCH_X86_64: return &x86_64_backend;
    case MINIC_TARGET_ARCH_AARCH64: return &aarch64_backend;
    case MINIC_TARGET_ARCH_LOONGARCH64: return &loongarch64_backend;
    }
    return NULL;
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
