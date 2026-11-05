#include "target/loongarch64/machine_scalar_source_select.h"

#include "target/common/core_machine_asm.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/machine_opcode.h"

#include <limits.h>
#include <stdint.h>

enum { SCALAR_T0 = 12U, SCALAR_T1 = 13U };

typedef struct LoongScalarSourceContext {
    MinicMachineBuilder *builder;
    const MinicDirectLP64BlockContext *ctx;
    const MinicCoreInstruction *ins;
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    size_t count;
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool);
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t);
    void *opaque;
} LoongScalarSourceContext;

static MinicMachineRegister scalar_register(unsigned int number,
    MinicMachineRegisterBank bank) {
    MinicMachineRegister result = {0};
    result.number = number;
    result.width_bits = 64U;
    result.bank = bank;
    return result;
}

static MinicMachineOperand scalar_gp(unsigned int number,
    unsigned int access, const char *constraint, const char *name) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = scalar_register(number,
        MINIC_MACHINE_REGISTER_GENERAL);
    result.value.physical.access = access;
    result.asm_constraint = (char *)constraint;
    result.asm_name = (char *)name;
    return result;
}

static MinicMachineOperand scalar_immediate(int64_t value,
    const char *constraint) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_INTEGER;
    result.value.integer = value;
    result.asm_constraint = (char *)constraint;
    return result;
}

static MinicMachineOperand scalar_address(unsigned int base,
    const char *constraint) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.target_mode = MINIC_LOONG_ADDRESS_IMM12;
    result.value.address.has_base = true;
    result.value.address.base = scalar_register(base,
        MINIC_MACHINE_REGISTER_GENERAL);
    result.value.address.scale = 1U;
    result.asm_constraint = (char *)constraint;
    return result;
}

/* Width/sign and output-lvalue checks are Core-typed, not recovered C AST
 * spelling. The original C source has already been sealed by TargetPolicy;
 * the Machine selector must nevertheless independently verify its roles. */
static bool scalar_integer(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool pointee, size_t width, int signed_mode) {
    MinicCoreTypeId id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return false;
    id = minic_core_type_unqualified(fn->type_context,
        fn->values[value].core_type_id);
    type = minic_core_type_get(fn->type_context, id);
    if (pointee) {
        if (type == NULL || type->kind != MINIC_CORE_TYPE_POINTER)
            return false;
        id = minic_core_type_unqualified(fn->type_context,
            type->value.pointer.pointee);
        type = minic_core_type_get(fn->type_context, id);
    }
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size == width &&
        !type->value.integer.is_bool &&
        type->value.integer.bit_width == width * 8U &&
        (signed_mode < 0 ||
         type->value.integer.is_signed == (signed_mode != 0));
}

static bool scalar_integer_any(const MinicCoreFunction *fn,
    MinicCoreValueId value) {
    MinicCoreTypeId id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return false;
    id = minic_core_type_unqualified(fn->type_context,
        fn->values[value].core_type_id);
    type = minic_core_type_get(fn->type_context, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size != 0U &&
        type->size <= 8U && !type->value.integer.is_bool &&
        type->value.integer.bit_width == type->size * 8U;
}

static bool scalar_role(const LoongScalarSourceContext *s, size_t index,
    MinicCoreStructuredInlineAsmOperandKind kind, bool physical_early,
    bool physical_readwrite) {
    const MinicCoreStructuredInlineAsmOperand *role;
    if (s == NULL || s->ctx == NULL || s->ctx->fn == NULL ||
        s->roles == NULL || index >= s->count) return false;
    role = &s->roles[index];
    return role->operand_index == index && role->kind == kind &&
        role->value < s->ctx->fn->value_count &&
        !role->early_clobber && !role->has_fixed_register_binding &&
        !role->has_tied_output &&
        role->physical_early_clobber == physical_early &&
        role->physical_read_write == physical_readwrite;
}

static bool scalar_constant(const MinicCoreFunction *fn,
    MinicCoreValueId value, int64_t *out) {
    MinicCoreInstructionId id;
    const MinicCoreInstruction *ins;
    if (!scalar_integer_any(fn, value) || fn->instructions == NULL ||
        (id = fn->values[value].definition) >= fn->instruction_count)
        return false;
    ins = &fn->instructions[id];
    if (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        ins->result != value) return false;
    if (out != NULL) *out = ins->value.integer_value;
    return true;
}

static bool scalar_emit(LoongScalarSourceContext *s,
    unsigned effects, const MinicMachineOperand *operands, size_t count,
    const MinicMachineRegisterEffect *implicit, size_t implicit_count) {
    return minic_machine_builder_emit_core_source_asm(s->builder,
        s->source, effects, operands, count, implicit, implicit_count,
        false, false);
}

/* Linux flush_cache_line: CACOP's opcode must be the original final Core
 * immediate 8..13 and its ZC memory binding supplies a real base address.
 * The source's physical cache effect is not a C read/store replacement. */
static bool scalar_cacop(LoongScalarSourceContext *s) {
    const MinicCoreFunction *fn = s->ctx->fn;
    MinicMachineOperand operands[2] = {{0}};
    int64_t opcode;
    if (s->count != 2U || !s->source->is_volatile ||
        !s->source->has_memory_clobber ||
        !s->source->implicit_memory_effect ||
        s->source->explicit_memory_clobber ||
        !scalar_role(s, 0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT, false, false) ||
        !scalar_role(s, 1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT, false, false) ||
        !scalar_constant(fn, s->roles[0].value, &opcode) ||
        opcode < 8 || opcode > 13 ||
        !scalar_integer(fn, s->roles[1].value, true, 1U, 0) ||
        !s->load(s->opaque, s->roles[1].value, SCALAR_T0, true))
        return false;
    operands[0] = scalar_immediate(opcode, "i");
    operands[1] = scalar_address(SCALAR_T0, "ZC");
    return scalar_emit(s, MINIC_MACHINE_EFFECT_MEMORY_READ,
        operands, 2U, NULL, 0U);
}

/* Six sealed read-only Linux trap-register CSR snapshots. The source
 * template encodes an exact CSR number; no runtime CSR index is admitted. */
static bool scalar_frametrace_csr(LoongScalarSourceContext *s) {
    const MinicCoreFunction *fn = s->ctx->fn;
    MinicMachineOperand operand;
    if (s->count != 1U || !s->source->is_volatile ||
        s->source->has_memory_clobber ||
        loong_linux_frametrace_csr_form(s->source->template_text,
            s->source->template_length) == UINT32_MAX ||
        !scalar_role(s, 0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
            false, false) ||
        !(scalar_integer(fn, s->roles[0].value, true, 4U, 0) ||
          scalar_integer(fn, s->roles[0].value, true, 8U, 0)))
        return false;
    operand = scalar_gp(SCALAR_T0, MINIC_MACHINE_REGISTER_WRITE,
        "=r", NULL);
    return scalar_emit(s, 0U, &operand, 1U, NULL, 0U) &&
        s->store(s->opaque, s->roles[0].value, SCALAR_T0,
            scalar_integer(fn, s->roles[0].value, true, 8U, 0)
                ? 8U : 4U);
}

static bool scalar_cpucfg(LoongScalarSourceContext *s) {
    const MinicCoreFunction *fn = s->ctx->fn;
    MinicMachineOperand operands[2] = {{0}};
    if (s->count != 2U || !s->source->is_volatile ||
        s->source->has_memory_clobber ||
        !scalar_role(s, 0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
            true, false) ||
        !scalar_role(s, 1U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
            false, false) ||
        !scalar_integer(fn, s->roles[0].value, true, 4U, 0) ||
        !scalar_integer_any(fn, s->roles[1].value) ||
        !s->load(s->opaque, s->roles[1].value, SCALAR_T1, true))
        return false;
    operands[0] = scalar_gp(SCALAR_T0, MINIC_MACHINE_REGISTER_WRITE,
        "=&r", NULL);
    operands[1] = scalar_gp(SCALAR_T1, MINIC_MACHINE_REGISTER_READ,
        "r", NULL);
    return scalar_emit(s, 0U, operands, 2U, NULL, 0U) &&
        s->store(s->opaque, s->roles[0].value, SCALAR_T0, 4U);
}

/* All 32 physical FPRs in both bit-preserving transfer directions. The
 * literal f-register is part of the exact sealed source template; declare
 * that physical register's read/write effect in MachineIR as well. */
static bool scalar_fpr_transfer(LoongScalarSourceContext *s) {
    const unsigned form = loong_linux_fpr_transfer_form(
        s->source->template_text, s->source->template_length);
    const bool read = form != 0U && form <= 32U;
    MinicMachineOperand operand;
    MinicMachineRegisterEffect fpr = {0};
    if (form == 0U || form > 64U || s->count != 1U ||
        !s->source->is_volatile || s->source->has_memory_clobber ||
        !scalar_role(s, 0U, read
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
            false, false) ||
        !scalar_integer(s->ctx->fn, s->roles[0].value, read, 8U, 0))
        return false;
    fpr.reg = scalar_register((form - 1U) % 32U,
        MINIC_MACHINE_REGISTER_FLOAT);
    fpr.access = read ? MINIC_MACHINE_REGISTER_READ :
        MINIC_MACHINE_REGISTER_WRITE;
    if (!read && !s->load(s->opaque,
            s->roles[0].value, SCALAR_T0, true)) return false;
    operand = scalar_gp(SCALAR_T0,
        read ? MINIC_MACHINE_REGISTER_WRITE :
            MINIC_MACHINE_REGISTER_READ,
        read ? "=r" : "r", NULL);
    return scalar_emit(s, 0U, &operand, 1U, &fpr, 1U) &&
        (!read || s->store(s->opaque,
            s->roles[0].value, SCALAR_T0, 8U));
}

static bool scalar_clock(LoongScalarSourceContext *s) {
    /* GNU named placeholders are normalized to numeric Core placeholders.
     * The sealed source form retains the named form identity, which is also
     * independently checked against the normalized grammar at dispatch. */
    const unsigned form = s->source->target_machine_form_variant;
    const size_t count = form == LOONG_CLOCK_VDSO_BASE ||
        form == LOONG_CLOCK_CPU_ID || form == LOONG_CLOCK_COUNTER_ONLY
        ? 1U : 2U;
    MinicMachineOperand operands[2] = {{0}};
    size_t widths[2] = {8U, 8U};
    size_t i;
    if (form == LOONG_CLOCK_NONE || form > LOONG_CLOCK_COUNTER_ONLY ||
        s->count != count ||
        /* Core canonicalizes observable GNU asm outputs as effectful even
         * when the sealed C ingress source was nonvolatile (VDSO form3). */
        !s->source->is_volatile ||
        s->source->has_memory_clobber != (form == LOONG_CLOCK_CPU_ID))
        return false;
    if (form == LOONG_CLOCK_RDTIME_NUMERIC) widths[1] = 4U;
    if (form == LOONG_CLOCK_CPU_ID) widths[0] = 4U;
    for (i = 0U; i < count; ++i) {
        const bool word32 = widths[i] == 4U;
        const bool early = form == LOONG_CLOCK_RDTIME_NAMED;
        if (!scalar_role(s, i,
                MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
                early, false) ||
            !scalar_integer(s->ctx->fn, s->roles[i].value,
                true, widths[i], word32 ? 1 : 0)) return false;
        operands[i] = scalar_gp(i == 0U ? SCALAR_T0 : SCALAR_T1,
            MINIC_MACHINE_REGISTER_WRITE, early ? "=&r" : "=r",
            form == LOONG_CLOCK_RDTIME_NAMED ?
                (i == 0U ? "val" : "tid") : NULL);
    }
    if (!scalar_emit(s, 0U, operands, count, NULL, 0U)) return false;
    for (i = 0U; i < count; ++i)
        if (!s->store(s->opaque, s->roles[i].value,
                i == 0U ? SCALAR_T0 : SCALAR_T1, widths[i])) return false;
    return true;
}

static bool scalar_invtlb_opcode(int64_t opcode) {
    return (opcode >= 0 && opcode <= 6) ||
        (opcode >= 9 && opcode <= 14) ||
        (opcode >= 16 && opcode <= 22);
}

static bool scalar_invtlb(LoongScalarSourceContext *s) {
    const MinicCoreFunction *fn = s->ctx->fn;
    const unsigned form = loong_linux_invtlb_form(
        s->source->template_text, s->source->template_length);
    const size_t count = form == 1U ? 3U : form == 4U ? 1U : 2U;
    MinicMachineOperand operands[3] = {{0}};
    int64_t opcode;
    size_t i;
    if (form == 0U || form > 4U || s->count != count ||
        !s->source->is_volatile || !s->source->has_memory_clobber ||
        !s->source->implicit_memory_effect ||
        !s->source->explicit_memory_clobber ||
        !scalar_constant(fn, s->roles[0].value, &opcode) ||
        !scalar_invtlb_opcode(opcode)) return false;
    for (i = 0U; i < count; ++i) {
        if (!scalar_role(s, i,
                MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
                false, false)) return false;
        if (i != 0U && !scalar_integer(fn, s->roles[i].value,
                false, (form == 1U && i == 1U) || form == 3U
                    ? 4U : 8U, -1)) return false;
    }
    operands[0] = scalar_immediate(opcode, "i");
    if (form == 1U || form == 3U) {
        if (!s->load(s->opaque, s->roles[1].value, SCALAR_T0, true))
            return false;
        operands[1] = scalar_gp(SCALAR_T0,
            MINIC_MACHINE_REGISTER_READ, "r", NULL);
    }
    if (form == 1U || form == 2U) {
        const size_t index = form == 1U ? 2U : 1U;
        if (!s->load(s->opaque, s->roles[index].value,
                SCALAR_T1, true)) return false;
        operands[index] = scalar_gp(SCALAR_T1,
            MINIC_MACHINE_REGISTER_READ, "r", NULL);
    }
    return scalar_emit(s, 0U, operands, count, NULL, 0U);
}

bool minic_loongarch64_machine_select_scalar_source(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    LoongScalarSourceContext s = {0};
    const MinicCoreFunction *fn;
    size_t source_id;
    uint32_t id, variant;
    if (builder == NULL || builder->function == NULL || ctx == NULL ||
        (fn = ctx->fn) == NULL || ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        builder->function->target != ctx->target || ins == NULL ||
        load == NULL || store == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        (source_id = ins->value.structured_inline_asm.inline_asm_id) >=
            fn->inline_asm_count || fn->inline_asms == NULL)
        return false;
    s.builder = builder;
    s.ctx = ctx;
    s.ins = ins;
    s.source = &fn->inline_asms[source_id];
    s.roles = ins->value.structured_inline_asm.operands;
    s.count = ins->value.structured_inline_asm.operand_count;
    s.load = load;
    s.store = store;
    s.opaque = opaque;
    id = s.source->target_machine_form_id;
    variant = s.source->target_machine_form_variant;
    if (s.count == 0U || s.roles == NULL || s.source->template_text == NULL ||
        s.source->is_goto || s.source->register_clobber_count != 0U ||
        s.source->operand_plan_owner !=
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        s.source->resolved_immediate_count != 0U ||
        s.source->goto_second_target != MINIC_CORE_BLOCK_INVALID)
        return false;
    if (id >= LOONG_DESC_CACOP && id <= LOONG_DESC_FPR_TRANSFER) {
        if ((id != LOONG_DESC_CACOP && id != LOONG_DESC_FRAMETRACE_CSR &&
             id != LOONG_DESC_CPUCFG && id != LOONG_DESC_FPR_TRANSFER) ||
            variant != 0U ||
            !minic_loongarch64_typed_descriptor_matches_template(id,
                variant, s.source->template_text,
                s.source->template_length)) return false;
    } else if (id == MINIC_LOONG_MACHINE_ASM_CLOCK ||
               id == MINIC_LOONG_MACHINE_ASM_INVTLB) {
        if (!minic_loongarch64_typed_scalar_form_matches_template(id,
                variant, s.source->template_text,
                s.source->template_length) &&
            !(id == MINIC_LOONG_MACHINE_ASM_CLOCK &&
              variant == LOONG_CLOCK_RDTIME_NAMED &&
              loong_linux_clock_form(s.source->template_text,
                  s.source->template_length) == LOONG_CLOCK_RDTIME_NUMERIC))
            return false;
    } else return false;
    switch (id) {
    case LOONG_DESC_CACOP: return scalar_cacop(&s);
    case LOONG_DESC_FRAMETRACE_CSR: return scalar_frametrace_csr(&s);
    case LOONG_DESC_CPUCFG: return scalar_cpucfg(&s);
    case LOONG_DESC_FPR_TRANSFER: return scalar_fpr_transfer(&s);
    case MINIC_LOONG_MACHINE_ASM_CLOCK: return scalar_clock(&s);
    case MINIC_LOONG_MACHINE_ASM_INVTLB: return scalar_invtlb(&s);
    default: return false;
    }
}
