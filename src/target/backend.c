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

static uint64_t minic_asm_hash_bytes(uint64_t hash,
                                     const char *data, size_t count) {
    size_t i;
    if (data == NULL) return hash;
    for (i = 0U; i < count; ++i) {
        hash ^= (unsigned char)data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t minic_asm_hash_size(uint64_t hash, size_t count) {
    uint64_t bits = (uint64_t)count;
    size_t i;
    for (i = 0U; i < 8U; ++i) {
        hash ^= (unsigned char)(bits & UINT64_C(255));
        hash *= UINT64_C(1099511628211);
        bits >>= 8U;
    }
    return hash;
}

/* Identity includes the exact ordered GNU source constraints, flags,
 * clobbers and template. It is deliberately ISA-independent and is not a
 * license for an emitter to skip its own machine-form validation. */
uint64_t minic_backend_asm_source_form_id(const MinicInlineAsm *source) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    if (source == NULL) return 0U;
    hash = minic_asm_hash_bytes(hash, source->template_text,
                                source->template_length);
    hash = minic_asm_hash_size(hash, source->output_count);
    hash = minic_asm_hash_size(hash, source->input_count);
    hash = minic_asm_hash_size(hash, source->label_count);
    hash = minic_asm_hash_size(hash, source->register_clobber_count);
    hash = minic_asm_hash_size(hash, source->clobber_count);
    hash = minic_asm_hash_size(hash, source->has_memory_clobber ? 1U : 0U);
    hash = minic_asm_hash_size(hash, source->is_goto ? 1U : 0U);
    hash = minic_asm_hash_size(hash, source->is_volatile ? 1U : 0U);
    for (i = 0U; i < source->output_count && source->outputs != NULL; ++i) {
        const MinicInlineAsmOperand *op = &source->outputs[i];
        hash = minic_asm_hash_bytes(hash, op->constraint_text,
                                    op->constraint_length);
        hash = minic_asm_hash_size(hash, (size_t)op->access);
    }
    for (i = 0U; i < source->input_count && source->inputs != NULL; ++i) {
        const MinicInlineAsmOperand *op = &source->inputs[i];
        hash = minic_asm_hash_bytes(hash, op->constraint_text,
                                    op->constraint_length);
        hash = minic_asm_hash_size(hash, (size_t)op->access);
    }
    for (i = 0U; i < source->register_clobber_count &&
                 source->register_clobbers != NULL; ++i) {
        const MinicInlineAsmRegisterClobber *clobber =
            &source->register_clobbers[i];
        hash = minic_asm_hash_bytes(hash, clobber->name,
                                    clobber->name_length);
    }
    return hash;
}

static void minic_asm_capture_tie(const MinicInlineAsm *source,
                                   const MinicInlineAsmOperand *input,
                                   MinicAsmOperandPlan *plan, size_t slot) {
    size_t i;
    if (input->constraint_length == 1U && input->constraint_text[0] >= '0' &&
        input->constraint_text[0] <= '9') {
        i = (size_t)(input->constraint_text[0] - '0');
        if (i < source->output_count) {
            plan->has_tied_output[slot] = true;
            plan->tied_output_index[slot] = i;
        }
        return;
    }
    if (input->constraint_length <= 2U || input->constraint_text[0] != '[' ||
        input->constraint_text[input->constraint_length - 1U] != ']') return;
    for (i = 0U; i < source->output_count; ++i) {
        const MinicInlineAsmOperand *output = &source->outputs[i];
        if (output->name && output->name_length == input->constraint_length - 2U &&
            memcmp(output->name, input->constraint_text + 1U,
                   output->name_length) == 0) {
            plan->has_tied_output[slot] = true;
            plan->tied_output_index[slot] = i;
            return;
        }
    }
}

bool minic_backend_capture_asm_operand_source(
    const MinicInlineAsm *source, MinicAsmOperandPlanOwner owner,
    MinicAsmOperandPlan *plan) {
    size_t i;
    bool effective_memory;
    if (!source || !plan || owner == MINIC_ASM_OPERAND_PLAN_NONE ||
        source->output_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT ||
        source->input_count > MINIC_CORE_STRUCTURED_INLINE_ASM_OPERAND_LIMIT -
                                  source->output_count ||
        plan->operand_count != source->output_count + source->input_count ||
        (source->output_count != 0U && source->outputs == NULL) ||
        (source->input_count != 0U && source->inputs == NULL)) return false;
    plan->owner = owner;
    plan->source_form_id = minic_backend_asm_source_form_id(source);
    plan->output_count = source->output_count;
    plan->explicit_memory_clobber = source->has_memory_clobber;
    memset(plan->gnu_early_clobber, 0, sizeof(plan->gnu_early_clobber));
    memset(plan->gnu_readwrite, 0, sizeof(plan->gnu_readwrite));
    memset(plan->has_tied_output, 0, sizeof(plan->has_tied_output));
    memset(plan->tied_output_index, 0, sizeof(plan->tied_output_index));
    effective_memory = source->has_memory_clobber || plan->implicit_memory_effect;
    for (i = 0U; i < plan->operand_count; ++i) {
        const bool output = i < source->output_count;
        const MinicInlineAsmOperand *op = output ? &source->outputs[i] :
            &source->inputs[i - source->output_count];
        if (!op->constraint_text || op->constraint_length == 0U ||
            (output && op->access == MINIC_INLINE_ASM_OPERAND_READ_ONLY) ||
            (!output && op->access != MINIC_INLINE_ASM_OPERAND_READ_ONLY))
            return false;
        plan->gnu_readwrite[i] =
            op->access == MINIC_INLINE_ASM_OPERAND_READ_WRITE;
        if (output)
            plan->gnu_early_clobber[i] =
                memchr(op->constraint_text, '&', op->constraint_length) != NULL;
        else
            minic_asm_capture_tie(source, op, plan, i);
        if (plan->kinds[i] == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            plan->implicit_memory_effect = true;
            effective_memory = true;
        }
    }
    plan->effective_memory_effect = effective_memory;
    return true;
}

bool minic_backend_qualify_inline_asm(
    const MinicBackend *backend, const MinicC0Program *program,
    const MinicInlineAsm *source, MinicTargetInlineAsmContract *contract) {
    if (!backend || !program || !source || !contract ||
        !backend->qualify_inline_asm) return false;
    memset(contract, 0, sizeof(*contract));
    return backend->qualify_inline_asm(program, source, contract) &&
           minic_backend_capture_asm_operand_source(
               source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, contract);
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
