#include "target/loongarch64/machine_csr_source_select.h"

#include "target/common/core_machine_asm.h"
#include "target/loongarch64/asm_contract.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/machine_asm_form.h"

#include <string.h>

enum { CSR_T0 = 12U, CSR_T1 = 13U };

static MinicMachineOperand csr_reg(unsigned int number,
    unsigned int access, const char *constraint, const char *name) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = (MinicMachineRegister){
        number, 64U, MINIC_MACHINE_REGISTER_GENERAL
    };
    result.value.physical.access = access;
    result.asm_constraint = (char *)constraint;
    result.asm_name = (char *)name;
    return result;
}

static MinicMachineOperand csr_zero_immediate(void) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_INTEGER;
    result.value.integer = 0;
    result.asm_constraint = "i";
    result.asm_name = "reg";
    return result;
}

/* Core's value type describes an output *address*, not a register. Check
 * its pointee independently of the original Source C declaration. */
static bool csr_unsigned_word(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool output, size_t *width) {
    const MinicCoreTypeContext *types;
    const MinicCoreType *type;
    MinicCoreTypeId id;
    if (fn == NULL || (types = fn->type_context) == NULL ||
        value >= fn->value_count || fn->values == NULL) return false;
    id = minic_core_type_unqualified(types, fn->values[value].core_type_id);
    type = minic_core_type_get(types, id);
    if (type == NULL) return false;
    if (output) {
        if (type->kind != MINIC_CORE_TYPE_POINTER ||
            !type->has_layout || type->size != 8U) return false;
        id = minic_core_type_unqualified(types, type->value.pointer.pointee);
        type = minic_core_type_get(types, id);
    }
    if (type == NULL || type->kind != MINIC_CORE_TYPE_INTEGER ||
        !type->has_layout || (type->size != 4U && type->size != 8U) ||
        type->value.integer.is_signed || type->value.integer.is_bool ||
        type->value.integer.bit_width != type->size * 8U) return false;
    if (!output && type->size != 4U) return false;
    if (width != NULL) *width = type->size;
    return true;
}

/* The CSR number must be a genuine final typed zero constant, not an SSA
 * conversion chain, an arbitrary register value or source-asm text. */
static bool csr_zero_definition(const MinicCoreFunction *fn,
    MinicCoreValueId value) {
    MinicCoreInstructionId definition;
    const MinicCoreInstruction *ins;
    if (fn == NULL || fn->values == NULL || fn->instructions == NULL ||
        value >= fn->value_count ||
        (definition = fn->values[value].definition) >=
            fn->instruction_count) return false;
    ins = &fn->instructions[definition];
    return ins->result == value &&
        ins->kind == MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT &&
        ins->value.integer_value == 0;
}

/* Linux arch_local_irq_save uses register unsigned mask asm("t0").
 * The source qualifier admitted precisely this one local fixed binding;
 * reading its Core SSA value into physical $t0 is its hardware identity. */
static bool csr_roles(const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins, unsigned int form, size_t *width) {
    const MinicCoreStructuredInlineAsmOperand *roles;
    const size_t count = form == 1U ? 2U : 3U;
    size_t i;
    if (fn == NULL || ins == NULL || width == NULL ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != count ||
        (roles = ins->value.structured_inline_asm.operands) == NULL)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *role = &roles[i];
        const bool output = i == 0U;
        if (role->operand_index != i || role->value >= fn->value_count ||
            role->kind != (output
                ? (form == 1U
                    ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
                    : MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE)
                : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT) ||
            role->early_clobber || role->physical_early_clobber ||
            role->physical_read_write != (output && form == 2U) ||
            role->has_tied_output)
            return false;
        if (role->has_fixed_register_binding) {
            const MinicCoreFixedRegisterBinding *fixed;
            if (form != 2U || i != 1U ||
                fn->fixed_register_bindings == NULL ||
                role->fixed_register_binding_id >=
                    fn->fixed_register_binding_count) return false;
            fixed = &fn->fixed_register_bindings[
                role->fixed_register_binding_id];
            if (!fixed->is_local || fixed->register_name == NULL ||
                fixed->register_name_length != 2U ||
                memcmp(fixed->register_name, "t0", 2U) != 0)
                return false;
        }
    }
    if (!csr_unsigned_word(fn, roles[0].value, true, width) ||
        (form == 1U && *width != 4U) ||
        (form == 2U &&
         !csr_unsigned_word(fn, roles[1].value, false, NULL)) ||
        !csr_zero_definition(fn, roles[count - 1U].value))
        return false;
    return true;
}

bool minic_loongarch64_machine_select_csr_source(
    MinicMachineBuilder *builder,
    const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load_value)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*read_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    bool (*write_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    MinicMachineOperand bindings[3] = {{0}};
    size_t source_id, width;
    unsigned int form;
    if (builder == NULL || builder->function == NULL ||
        ctx == NULL || (fn = ctx->fn) == NULL ||
        ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        builder->function->target != ctx->target ||
        ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        load_value == NULL || read_lvalue == NULL ||
        write_lvalue == NULL ||
        (source_id = ins->value.structured_inline_asm.inline_asm_id) >=
            fn->inline_asm_count || fn->inline_asms == NULL)
        return false;
    source = &fn->inline_asms[source_id];
    form = loong_csr_template(source->template_text, source->template_length);
    if ((form != 1U && form != 2U) ||
        source->target_machine_form_id != MINIC_LOONG_MACHINE_ASM_CSR ||
        source->target_machine_form_variant != form ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        !source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->deferred_immediate_symbol != NULL ||
        !minic_loongarch64_typed_scalar_form_matches_template(
            MINIC_LOONG_MACHINE_ASM_CSR, form,
            source->template_text, source->template_length) ||
        !csr_roles(fn, ins, form, &width)) return false;

    roles = ins->value.structured_inline_asm.operands;
    /* Deliberately use T1 as the old/new CSR carrier, T0 as mask. An
     * output-lvalue read may require an alignment-safe byte slow path;
     * the callback owns that CFG, and the CSR instruction sees its value. */
    if (form == 2U &&
        (!read_lvalue(opaque, roles[0].value, CSR_T1, width) ||
         !load_value(opaque, roles[1].value, CSR_T0, false)))
        return false;

    bindings[0] = csr_reg(CSR_T1,
        form == 1U ? MINIC_MACHINE_REGISTER_WRITE :
            (MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE),
        form == 1U ? "=r" : "+r", "val");
    if (form == 2U)
        bindings[1] = csr_reg(CSR_T0, MINIC_MACHINE_REGISTER_READ,
            "r", "mask");
    bindings[form == 1U ? 1U : 2U] = csr_zero_immediate();

    /* SOURCE_ASM retains the sealed source template and target form id;
     * inserting an ordinary C load/store in place of CSRXCHG is forbidden.
     * Both CSR effects are hardware-visible even without an ELF CALL. */
    return minic_machine_builder_emit_core_source_asm(builder, source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, form == 1U ? 2U : 3U,
            NULL, 0U, false, false) &&
        write_lvalue(opaque, roles[0].value, CSR_T1, width);
}
