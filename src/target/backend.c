#include "target/backend.h"

#include "target/riscv64/codegen.h"
#include "target/riscv32/rv32_emit.h"
#include "target/x86_64/codegen.h"
#include "target/x86_64/asm_contract.h"
#include "target/aarch64/codegen.h"
#include "target/aarch64/asm_contract.h"
#include "target/loongarch64/codegen.h"
#include "target/loongarch64/asm_contract.h"
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
/* Reserved source ISA templates are declared in their native target. */
static const MinicBackend riscv64_backend = {
    MINIC_TARGET_ARCH_RISCV64, "riscv64", emit_riscv64, NULL, true, NULL, NULL
};
static const MinicBackend riscv32_backend = {
    MINIC_TARGET_ARCH_RISCV32, "riscv32", minic_riscv32_write_core_program,
    NULL, true, NULL, NULL
};
static const MinicBackend x86_64_backend = {
    MINIC_TARGET_ARCH_X86_64, "x86_64",
    minic_x86_64_write_c0_program_with_core_functions,
    minic_x86_64_qualify_inline_asm, false,
    minic_x86_64_requires_qualified_input_asm,
    minic_x86_64_qualify_inline_asm_goto
};
static const MinicBackend aarch64_backend = {
    MINIC_TARGET_ARCH_AARCH64, "aarch64",
    minic_aarch64_write_c0_program_with_core_functions,
    minic_aarch64_qualify_inline_asm, false,
    minic_aarch64_requires_qualified_input_asm,
    minic_aarch64_qualify_inline_asm_goto
};
static const MinicBackend loongarch64_backend = {
    MINIC_TARGET_ARCH_LOONGARCH64, "loongarch64",
    minic_loongarch64_write_c0_program_with_core_functions,
    minic_loongarch64_qualify_inline_asm, false,
    minic_loongarch64_requires_qualified_input_asm,
    minic_loongarch64_qualify_inline_asm_goto
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

/* Shared lexical guard; one ISA may reserve source prefix families without
 * a fallback turning unsupported hardware operations into plain Core ASM.
 * The machine operand/constraint/encoding checks stay solely in the target. */
bool minic_backend_inline_asm_prefix_matches(
    const MinicInlineAsm *src, const char *const *prefixes, size_t count) {
    const char *p;
    size_t n, start, i;
    if (!src || src->input_count == 0U || !src->template_text ||
        src->template_length == 0U || !prefixes || count == 0U)
        return false;
    p = src->template_text;
    n = src->template_length;
    start = 0U;
    while (start < n && (p[start] == ' ' || p[start] == '\t' ||
                         p[start] == '\r' || p[start] == '\n')) ++start;
    for (i = 0U; i < count; ++i) {
        const char *mnemonic = prefixes[i];
        size_t len;
        if (!mnemonic) continue;
        len = strlen(mnemonic);
        if (len == 0U || len > n - start || memcmp(p + start, mnemonic, len))
            continue;
        if (start + len == n || p[start + len] == ' ' ||
            p[start + len] == '\t' || p[start + len] == '\r' ||
            p[start + len] == '\n' || p[start + len] == '%' ||
            (len >= 2U && mnemonic[0] == '/' && mnemonic[1] == '/' &&
             p[start + len] == '_'))
            return true;
    }
    return false;
}

bool minic_backend_requires_qualified_input_asm(
    const MinicBackend *backend, const MinicInlineAsm *src) {
    return backend && backend->requires_qualified_input_asm &&
           backend->requires_qualified_input_asm(src);
}

bool minic_backend_qualify_inline_asm_goto(
    const MinicBackend *backend, const MinicC0Program *program,
    const MinicInlineAsm *source) {
    return backend && backend->qualify_inline_asm_goto &&
           backend->qualify_inline_asm_goto(program, source);
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
