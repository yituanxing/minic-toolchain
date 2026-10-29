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
    .emit_program = emit_riscv64,
    .emit_reachable_core_blocks_only = true,
};
static const MinicBackend riscv32_backend = {
    .emit_program = minic_riscv32_write_core_program,
    .emit_reachable_core_blocks_only = true,
};
static const MinicBackend x86_64_backend = {
    .emit_program = minic_x86_64_write_c0_program_with_core_functions,
    .qualify_inline_asm = minic_x86_64_qualify_inline_asm,
    .requires_qualified_input_asm = minic_x86_64_requires_qualified_input_asm,
    .qualify_inline_asm_goto = minic_x86_64_qualify_inline_asm_goto,
};
static const MinicBackend aarch64_backend = {
    .emit_program = minic_aarch64_write_c0_program_with_core_functions,
    .qualify_inline_asm = minic_aarch64_qualify_inline_asm,
    .requires_qualified_input_asm = minic_aarch64_requires_qualified_input_asm,
    .qualify_inline_asm_goto = minic_aarch64_qualify_inline_asm_goto,
};
static const MinicBackend loongarch64_backend = {
    .emit_program = minic_loongarch64_write_c0_program_with_core_functions,
    .qualify_inline_asm = minic_loongarch64_qualify_inline_asm,
    .requires_qualified_input_asm = minic_loongarch64_requires_qualified_input_asm,
    .qualify_inline_asm_goto = minic_loongarch64_qualify_inline_asm_goto,
};

/* Generated from the same target inventory as the separately linked driver
 * catalog and TargetInfo. This removes the independent architecture switch
 * and prevents a new target from silently selecting another ISA's emitter. */
static const MinicBackend *const minic_registered_backends[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    &id##_backend,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

const MinicBackend *minic_backend_for_target(const MinicTargetInfo *target) {
    const MinicTargetCatalogEntry *catalog;
    size_t count, index;
    if (target == NULL) return NULL;
    catalog = minic_target_catalog_all(&count);
    if (count != sizeof(minic_registered_backends) /
                 sizeof(minic_registered_backends[0])) return NULL;
    for (index = 0U; index < count; ++index) {
        if (catalog[index].architecture == target->architecture)
            return minic_registered_backends[index];
    }
    return NULL;
}

bool minic_backend_qualify_inline_asm(
    const MinicBackend *backend, const MinicC0Program *program,
    const MinicInlineAsm *source, MinicTargetInlineAsmContract *contract) {
    size_t i;
    if (!backend || !program || !source || !contract ||
        !backend->qualify_inline_asm) return false;
    memset(contract, 0, sizeof(*contract));
    if (!backend->qualify_inline_asm(program, source, contract) ||
        contract->operand_count != source->output_count + source->input_count ||
        contract->operand_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        (source->output_count != 0U && source->outputs == NULL) ||
        (source->input_count != 0U && source->inputs == NULL))
        return false;
    memset(contract->gnu_early_clobber, 0,
           sizeof(contract->gnu_early_clobber));
    /* Only the GNU output operand can declare early clobber. Keep the
     * original source fact even if the target maps its storage into a
     * different Core transport kind. This is independent of the ISA. */
    for (i = 0U; i < source->output_count; ++i) {
        const MinicInlineAsmOperand *op = &source->outputs[i];
        if (op->constraint_text == NULL || op->constraint_length == 0U)
            return false;
        contract->gnu_early_clobber[i] =
            memchr(op->constraint_text, '&', op->constraint_length) != NULL;
    }
    return true;
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
        backend != minic_backend_for_target(target) ||
        backend->emit_program == NULL)
        return false;
    return backend->emit_program(output_path, program, target, functions,
                                 function_count, function_sections, diagnostic);
}
