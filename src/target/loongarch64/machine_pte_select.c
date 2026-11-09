#include "target/loongarch64/machine_pte_select.h"

#include "target/common/core_machine_asm.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/machine_opcode.h"
#include <string.h>

enum { PTE_T0 = 12U, PTE_T1 = 13U, PTE_T4 = 16U };

static MinicMachineRegister pte_register(unsigned int number) {
    MinicMachineRegister reg = {0};
    reg.number = number;
    reg.width_bits = 64U;
    reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    return reg;
}

static MinicMachineOperand pte_gp(unsigned int number,
    unsigned int access, const char *constraint, const char *name) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_REGISTER;
    operand.value.physical.reg = pte_register(number);
    operand.value.physical.access = access;
    operand.asm_constraint = (char *)constraint;
    operand.asm_name = (char *)name;
    return operand;
}

static MinicMachineOperand pte_memory(unsigned int base) {
    MinicMachineOperand operand;
    (void)memset(&operand, 0, sizeof(operand));
    operand.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    operand.value.address.has_base = true;
    operand.value.address.base = pte_register(base);
    operand.value.address.scale = 1U;
    operand.value.address.target_mode = MINIC_LOONG_ADDRESS_IMM12;
    operand.asm_constraint = "+m";
    operand.asm_name = "buddy";
    return operand;
}

static bool pte_unsigned_word(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool address) {
    MinicCoreTypeId type_id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return false;
    type_id = fn->values[value].core_type_id;
    if (address && !minic_lp64_core_pointer_pointee(
            fn->type_context, type_id, &type_id)) return false;
    type_id = minic_core_type_unqualified(fn->type_context, type_id);
    type = minic_core_type_get(fn->type_context, type_id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size == 8U &&
        !type->value.integer.is_bool &&
        !type->value.integer.is_signed &&
        type->value.integer.bit_width == 64U;
}

static bool pte_source(const MinicCoreInlineAsm *source) {
    return source != NULL &&
        source->target_machine_form_id == LOONG_DESC_PTE &&
        source->target_machine_form_variant == 0U &&
        source->operand_plan_owner == MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED &&
        source->is_volatile && !source->is_goto &&
        /* GNU +m owns the effect; the original source did not spell an
         * additional compiler-wide "memory" clobber. */
        source->has_memory_clobber && source->implicit_memory_effect &&
        !source->explicit_memory_clobber &&
        source->register_clobber_count == 0U &&
        source->resolved_immediate_count == 0U &&
        source->goto_second_target == MINIC_CORE_BLOCK_INVALID &&
        source->template_text != NULL &&
        loong_pte_form(source->template_text, source->template_length) &&
        minic_loongarch64_typed_descriptor_matches_template(
            LOONG_DESC_PTE, 0U,
            source->template_text, source->template_length);
}

bool minic_loongarch64_machine_select_pte(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3] = {{0}};
    size_t id, i;
    if (builder == NULL || builder->function == NULL ||
        ctx == NULL || (fn = ctx->fn) == NULL || ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        builder->function->target != ctx->target ||
        ins == NULL || load == NULL || store == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        fn->inline_asms == NULL ||
        (id = ins->value.structured_inline_asm.inline_asm_id) >=
            fn->inline_asm_count ||
        !pte_source(&fn->inline_asms[id]) ||
        ins->value.structured_inline_asm.operand_count != 3U ||
        (roles = ins->value.structured_inline_asm.operands) == NULL)
        return false;
    source = &fn->inline_asms[id];
    for (i = 0U; i < 3U; ++i) {
        const MinicCoreStructuredInlineAsmOperandKind expect = i == 0U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE
            : i == 1U
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        if (roles[i].operand_index != i || roles[i].kind != expect ||
            roles[i].has_fixed_register_binding ||
            roles[i].has_tied_output || roles[i].early_clobber ||
            /* Core transport has no logical early-clobber here.  The
             * sealed GNU `=&r` is an independent physical constraint. */
            roles[i].physical_early_clobber != (i == 1U) ||
            roles[i].physical_read_write != (i == 0U) ||
            !pte_unsigned_word(fn, roles[i].value, i < 2U))
            return false;
    }
    /* Physical source contract: original LL/SC memory lives at t1, the
     * global-bit value at t0, and the LL/SC status/old-value at t4.
     * T3 is left free for large-frame transport.  The original exact GNU
     * template retains `1:` retry, `2:` exit and `dbar 0x700` ordering. */
    if (!load(opaque, roles[2].value, PTE_T0, false) ||
        !load(opaque, roles[0].value, PTE_T1, true))
        return false;
    bindings[0] = pte_memory(PTE_T1);
    bindings[1] = pte_gp(PTE_T4, MINIC_MACHINE_REGISTER_WRITE,
        "=&r", "tmp");
    bindings[2] = pte_gp(PTE_T0, MINIC_MACHINE_REGISTER_READ,
        "r", "global");
    return minic_machine_builder_emit_core_source_asm(builder, source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, 3U, NULL, 0U, false, false) &&
        store(opaque, roles[1].value, PTE_T4, 8U);
}
