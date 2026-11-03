#include "target/backend_source.h"
#include "frontend/ast.h"
#include "target/target_catalog.h"
#include "target/x86_64/asm_contract_source.h"
#include "target/aarch64/asm_contract_source.h"
#include "target/loongarch64/asm_contract_source.h"

#include <string.h>

/* Source policy and machine emission intentionally register independently.
 * Both indexes come from the same canonical target inventory; there is no
 * frontend or Core architecture switch and no host-ISA fallback. */
static const MinicSourceBackend riscv64_source_backend = {0};
static const MinicSourceBackend riscv32_source_backend = {0};
static const MinicSourceBackend x86_64_source_backend = {
    .qualify_inline_asm = minic_x86_64_qualify_inline_asm,
    .requires_qualified_input_asm = minic_x86_64_requires_qualified_input_asm,
    .qualify_inline_asm_goto = minic_x86_64_qualify_inline_asm_goto,
};
static const MinicSourceBackend aarch64_source_backend = {
    .qualify_inline_asm = minic_aarch64_qualify_inline_asm,
    .seal_normalized_inline_asm = minic_aarch64_seal_normalized_inline_asm,
    .requires_qualified_input_asm = minic_aarch64_requires_qualified_input_asm,
    .qualify_inline_asm_goto = minic_aarch64_qualify_inline_asm_goto,
    .clone_safe_opaque_asm = minic_aarch64_clone_safe_opaque_asm,
};
static const MinicSourceBackend loongarch64_source_backend = {
    .qualify_inline_asm = minic_loongarch64_qualify_inline_asm,
    .seal_normalized_inline_asm = minic_loongarch64_seal_normalized_inline_asm,
    .requires_qualified_input_asm = minic_loongarch64_requires_qualified_input_asm,
    .qualify_inline_asm_goto = minic_loongarch64_qualify_inline_asm_goto,
};

static const MinicSourceBackend *const registered_source_backends[] = {
#define MINIC_TARGET_REGISTRATION(arch, id, spelling, stages, isa) \
    &id##_source_backend,
#include "target/target_registration.def"
#undef MINIC_TARGET_REGISTRATION
};

const MinicSourceBackend *minic_source_backend_for_target(
    const MinicTargetInfo *target) {
    const MinicTargetCatalogEntry *catalog;
    size_t count, index;
    if (target == NULL) return NULL;
    catalog = minic_target_catalog_all(&count);
    if (count != sizeof(registered_source_backends) /
                 sizeof(registered_source_backends[0])) return NULL;
    for (index = 0U; index < count; ++index)
        if (catalog[index].architecture == target->architecture)
            return registered_source_backends[index];
    return NULL;
}

static uint64_t source_asm_hash_bytes(uint64_t hash,
                                      const char *data, size_t count) {
    size_t i;
    if (data == NULL) return hash;
    for (i = 0U; i < count; ++i) {
        hash ^= (unsigned char)data[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t source_asm_hash_size(uint64_t hash, size_t count) {
    uint64_t bits = (uint64_t)count;
    size_t i;
    for (i = 0U; i < 8U; ++i) {
        hash ^= (unsigned char)(bits & UINT64_C(255));
        hash *= UINT64_C(1099511628211);
        bits >>= 8U;
    }
    return hash;
}

/* This source fingerprint covers ordered GNU constraints, flags, clobbers
 * and template. It is diagnostic provenance, not an ISA opcode/cache key. */
uint64_t minic_source_backend_asm_source_form_id(const MinicInlineAsm *source) {
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t i;
    if (source == NULL) return 0U;
    hash = source_asm_hash_bytes(hash, source->template_text,
                                 source->template_length);
    hash = source_asm_hash_size(hash, source->output_count);
    hash = source_asm_hash_size(hash, source->input_count);
    hash = source_asm_hash_size(hash, source->label_count);
    hash = source_asm_hash_size(hash, source->register_clobber_count);
    hash = source_asm_hash_size(hash, source->clobber_count);
    hash = source_asm_hash_size(hash, source->has_memory_clobber ? 1U : 0U);
    hash = source_asm_hash_size(hash, source->is_goto ? 1U : 0U);
    hash = source_asm_hash_size(hash, source->is_volatile ? 1U : 0U);
    for (i = 0U; i < source->output_count && source->outputs != NULL; ++i) {
        const MinicInlineAsmOperand *op = &source->outputs[i];
        hash = source_asm_hash_bytes(hash, op->constraint_text,
                                     op->constraint_length);
        hash = source_asm_hash_size(hash, (size_t)op->access);
    }
    for (i = 0U; i < source->input_count && source->inputs != NULL; ++i) {
        const MinicInlineAsmOperand *op = &source->inputs[i];
        hash = source_asm_hash_bytes(hash, op->constraint_text,
                                     op->constraint_length);
        hash = source_asm_hash_size(hash, (size_t)op->access);
    }
    for (i = 0U; i < source->register_clobber_count &&
                 source->register_clobbers != NULL; ++i) {
        const MinicInlineAsmRegisterClobber *clobber =
            &source->register_clobbers[i];
        hash = source_asm_hash_bytes(hash, clobber->name,
                                     clobber->name_length);
    }
    return hash;
}

static void source_asm_capture_tie(const MinicInlineAsm *source,
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

bool minic_source_backend_capture_asm_operand_source(
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
    plan->source_form_id = minic_source_backend_asm_source_form_id(source);
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
            source_asm_capture_tie(source, op, plan, i);
        if (plan->kinds[i] == MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE) {
            plan->implicit_memory_effect = true;
            effective_memory = true;
        }
    }
    plan->effective_memory_effect = effective_memory;
    return true;
}

bool minic_source_backend_qualify_inline_asm(
    const MinicSourceBackend *backend, const MinicTargetInvocation *invocation,
    const MinicC0Program *program,
    const MinicInlineAsm *source, MinicTargetInlineAsmContract *contract) {
    if (!backend || !invocation || !program || !source || !contract ||
        !backend->qualify_inline_asm) return false;
    memset(contract, 0, sizeof(*contract));
    return backend->qualify_inline_asm(invocation, program, source, contract) &&
           minic_source_backend_capture_asm_operand_source(
               source, MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED, contract);
}

bool minic_source_backend_seal_normalized_inline_asm(
    const MinicSourceBackend *backend, const MinicTargetInvocation *invocation,
    const MinicC0Program *program,
    const MinicInlineAsm *source, const char *normalized, size_t length,
    MinicTargetInlineAsmContract *contract) {
    MinicTargetInlineAsmContract before, after;
    if (!backend || !invocation || !program || !source || !normalized || !contract ||
        contract->owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED)
        return false;
    if (!backend->seal_normalized_inline_asm) return true;
    before = *contract;
    if (!backend->seal_normalized_inline_asm(
            invocation, program, source, normalized, length, contract))
        return false;
    /* A machine seal cannot change already-qualified operand/effect facts. */
    after = *contract;
    after.target_machine_form_id = before.target_machine_form_id;
    after.target_machine_form_variant = before.target_machine_form_variant;
    return memcmp(&before, &after, sizeof(before)) == 0;
}

bool minic_source_backend_inline_asm_prefix_matches(
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

bool minic_source_backend_requires_qualified_input_asm(
    const MinicSourceBackend *backend, const MinicInlineAsm *src) {
    return backend && backend->requires_qualified_input_asm &&
           backend->requires_qualified_input_asm(src);
}

bool minic_source_backend_qualify_inline_asm_goto(
    const MinicSourceBackend *backend, const MinicTargetInvocation *invocation,
    const MinicC0Program *program,
    const MinicInlineAsm *source) {
    return backend && invocation && backend->qualify_inline_asm_goto &&
           backend->qualify_inline_asm_goto(invocation, program, source);
}

bool minic_source_backend_clone_safe_opaque_asm(
    const MinicSourceBackend *backend, const MinicInlineAsm *source) {
    return backend && backend->clone_safe_opaque_asm && source &&
           backend->clone_safe_opaque_asm(source);
}
