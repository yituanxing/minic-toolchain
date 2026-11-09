#include "target/loongarch64/machine_uaccess_select.h"

#include "target/common/core_machine_asm.h"
#include "target/loongarch64/asm_contract.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/machine_opcode.h"

#include <stdint.h>
#include <string.h>

enum { UACCESS_T0 = 12U, UACCESS_T1 = 13U,
       UACCESS_T2 = 14U, UACCESS_T4 = 16U };

static MinicMachineRegister uaccess_register(unsigned int number) {
    MinicMachineRegister reg = {0};
    reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    reg.width_bits = 64U;
    reg.number = number;
    return reg;
}

static MinicMachineOperand uaccess_gp(unsigned int number,
    unsigned int access, const char *constraint) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_REGISTER;
    operand.value.physical.reg = uaccess_register(number);
    operand.value.physical.access = access;
    operand.asm_constraint = (char *)constraint;
    return operand;
}

static MinicMachineOperand uaccess_memory(unsigned int number,
    const char *constraint) {
    MinicMachineOperand operand;
    (void)memset(&operand, 0, sizeof(operand));
    operand.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    operand.value.address.has_base = true;
    operand.value.address.base = uaccess_register(number);
    operand.value.address.scale = 1U;
    operand.value.address.target_mode = MINIC_LOONG_ADDRESS_IMM12;
    operand.asm_constraint = (char *)constraint;
    return operand;
}

static const MinicCoreType *uaccess_type(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool pointee) {
    MinicCoreTypeId id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return NULL;
    id = minic_core_type_unqualified(fn->type_context,
        fn->values[value].core_type_id);
    type = minic_core_type_get(fn->type_context, id);
    if (pointee) {
        if (type == NULL || type->kind != MINIC_CORE_TYPE_POINTER ||
            !type->has_layout || type->size != 8U) return NULL;
        id = minic_core_type_unqualified(fn->type_context,
            type->value.pointer.pointee);
        type = minic_core_type_get(fn->type_context, id);
    }
    return type;
}

static bool uaccess_error_type(const MinicCoreFunction *fn,
    MinicCoreValueId value) {
    const MinicCoreType *type = uaccess_type(fn, value, true);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size == 4U &&
        type->value.integer.bit_width == 32U &&
        type->value.integer.is_signed && !type->value.integer.is_bool;
}

/* A data GPR's C carrier can be wider than the faulting user-memory
 * instruction (ld.b into unsigned long is an original GNU Linux form). */
static bool uaccess_data_type(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool pointee, size_t fault_width,
    size_t *carrier_width) {
    const MinicCoreType *type = uaccess_type(fn, value, pointee);
    size_t width;
    if (type == NULL || !type->has_layout ||
        (width = type->size) < fault_width || width > 8U) return false;
    if (type->kind == MINIC_CORE_TYPE_POINTER) {
        if (fault_width != 8U || width != 8U) return false;
    } else if (type->kind != MINIC_CORE_TYPE_INTEGER ||
               type->value.integer.is_bool ||
               type->value.integer.bit_width != width * 8U)
        return false;
    if (carrier_width != NULL) *carrier_width = width;
    return true;
}

/* The original `m` may describe a scalar user object or Linux's artificial
 * 800-byte `__large_struct` address dependency. C record names are absent
 * from Core: verify the latter by its complete ABI shape instead. */
static bool uaccess_memory_type(const MinicCoreFunction *fn,
    MinicCoreValueId address, size_t fault_width) {
    const MinicCoreTypeContext *types;
    const MinicCoreType *memory, *field, *element;
    MinicCoreTypeId id;
    if (fn == NULL || (types = fn->type_context) == NULL ||
        (memory = uaccess_type(fn, address, true)) == NULL)
        return false;
    if (memory->kind == MINIC_CORE_TYPE_POINTER)
        return fault_width == 8U && memory->size == 8U;
    if (memory->kind == MINIC_CORE_TYPE_INTEGER)
        return memory->has_layout && memory->size == fault_width &&
            !memory->value.integer.is_bool &&
            memory->value.integer.bit_width == fault_width * 8U;
    if (memory->kind != MINIC_CORE_TYPE_RECORD ||
        !memory->has_layout || !memory->value.record.is_complete ||
        memory->size != 800U || memory->value.record.field_count != 1U ||
        memory->value.record.fields == NULL ||
        memory->value.record.fields[0].byte_offset != 0U ||
        memory->value.record.fields[0].is_bit_field)
        return false;
    id = minic_core_type_unqualified(types,
        memory->value.record.fields[0].type_id);
    field = minic_core_type_get(types, id);
    if (field == NULL || field->kind != MINIC_CORE_TYPE_ARRAY ||
        !field->value.array.is_complete || field->value.array.count != 100U)
        return false;
    id = minic_core_type_unqualified(types, field->value.array.element);
    element = minic_core_type_get(types, id);
    return element != NULL && element->kind == MINIC_CORE_TYPE_INTEGER &&
        element->has_layout && element->size == 8U &&
        element->value.integer.bit_width == 64U &&
        !element->value.integer.is_signed &&
        !element->value.integer.is_bool;
}

static bool uaccess_role(const MinicCoreFunction *fn,
    const MinicCoreStructuredInlineAsmOperand *role, size_t ordinal,
    MinicCoreStructuredInlineAsmOperandKind expected, bool readwrite) {
    return fn != NULL && role != NULL &&
        role->operand_index == ordinal && role->value < fn->value_count &&
        role->kind == expected && !role->early_clobber &&
        !role->physical_early_clobber &&
        role->physical_read_write == readwrite &&
        !role->has_fixed_register_binding && !role->has_tied_output;
}

bool minic_loongarch64_machine_select_uaccess(
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
    LoongLinuxUaccess form = {0};
    size_t source_id, data_width;
    unsigned int variant;
    if (builder == NULL || builder->function == NULL || ctx == NULL ||
        (fn = ctx->fn) == NULL || ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        builder->function->target != ctx->target ||
        ins == NULL || load_value == NULL || read_lvalue == NULL ||
        write_lvalue == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        ins->result != MINIC_CORE_VALUE_INVALID ||
        (source_id = ins->value.structured_inline_asm.inline_asm_id) >=
            fn->inline_asm_count || fn->inline_asms == NULL ||
        ins->value.structured_inline_asm.operand_count != 3U)
        return false;
    source = &fn->inline_asms[source_id];
    roles = ins->value.structured_inline_asm.operands;
    if (roles == NULL || source->template_text == NULL ||
        source->target_machine_form_id != MINIC_LOONG_MACHINE_ASM_UACCESS ||
        source->operand_plan_owner !=
            MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        !source->implicit_memory_effect ||
        source->explicit_memory_clobber ||
        source->register_clobber_count != 0U ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        !loong_linux_uaccess_form(source->template_text,
            source->template_length, &form))
        return false;
    variant = (form.load ? 0U : 4U) +
        (form.width == 1U ? 1U : form.width == 2U ? 2U :
         form.width == 4U ? 3U : 4U);
    if (source->target_machine_form_variant != variant ||
        !minic_loongarch64_typed_scalar_form_matches_template(
            MINIC_LOONG_MACHINE_ASM_UACCESS, variant,
            source->template_text, source->template_length) ||
        !uaccess_role(fn, &roles[0], 0U,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE, true) ||
        !uaccess_role(fn, &roles[1], 1U, form.load
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT, false) ||
        !uaccess_role(fn, &roles[2], 2U, form.load
            ? MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT
            : MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT, false) ||
        !uaccess_error_type(fn, roles[0].value) ||
        !uaccess_data_type(fn,
            roles[form.load ? 1U : 2U].value, form.load,
            form.width, &data_width) ||
        !uaccess_memory_type(fn,
            roles[form.load ? 2U : 1U].value, form.width))
        return false;

    /* +r error is an actual input/output lvalue. Read its prior contents
     * with the selector's guarded 4-byte alignment-aware ingress, then
     * retain its value in t0 across the faulting source instruction. */
    if (!read_lvalue(opaque, roles[0].value, UACCESS_T0, 4U) ||
        !load_value(opaque,
            roles[form.load ? 2U : 1U].value, UACCESS_T4, true) ||
        (!form.load && !load_value(opaque,
            roles[2].value, UACCESS_T2, true)))
        return false;

    bindings[0] = uaccess_gp(UACCESS_T0,
        MINIC_MACHINE_REGISTER_READ | MINIC_MACHINE_REGISTER_WRITE, "+r");
    bindings[1] = form.load
        ? uaccess_gp(UACCESS_T1, MINIC_MACHINE_REGISTER_WRITE, "=r")
        : uaccess_memory(UACCESS_T4, "=m");
    bindings[2] = form.load
        ? uaccess_memory(UACCESS_T4, "m")
        : uaccess_gp(UACCESS_T2, MINIC_MACHINE_REGISTER_READ, "Jr");

    /* Keep the exact originally sealed GNU LL/ST fault table, `.equ/.irp`
     * register number definitions and PREL exception relocations. The
     * serializer expands `%0/%1/%2` against actual typed GPR/address
     * bindings, not text generated from the compiler's machine opcodes. */
    return minic_machine_builder_emit_core_source_asm(builder, source,
            form.load ? MINIC_MACHINE_EFFECT_MEMORY_READ
                      : MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings, 3U, NULL, 0U, false, false) &&
        write_lvalue(opaque, roles[0].value, UACCESS_T0, 4U) &&
        (!form.load || write_lvalue(opaque,
            roles[1].value, UACCESS_T1, data_width));
}
