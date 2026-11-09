#include "target/loongarch64/machine_llsc_extended_select.h"

#include "target/common/core_machine_asm.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/machine_opcode.h"

#include <stdint.h>
#include <string.h>

enum {
    EXT_T0 = 12U, EXT_T1 = 13U, EXT_T2 = 14U,
    EXT_T4 = 16U, EXT_T5 = 17U, EXT_T6 = 18U,
    EXT_T7 = 19U, EXT_T8 = 20U
};

typedef struct ExtendedSelection {
    MinicMachineBuilder *builder;
    const MinicCoreFunction *fn;
    const MinicCoreInstruction *instruction;
    const MinicCoreInlineAsm *source;
    const MinicCoreStructuredInlineAsmOperand *roles;
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool);
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t);
    bool (*read_lvalue)(void *, MinicCoreValueId, unsigned int, size_t);
    void *opaque;
} ExtendedSelection;

static MinicMachineRegister ext_reg(unsigned int number) {
    MinicMachineRegister result = {0};
    result.number = number;
    result.width_bits = 64U;
    result.bank = MINIC_MACHINE_REGISTER_GENERAL;
    return result;
}

static MinicMachineOperand ext_gp(unsigned int number,
    unsigned int access, const char *constraint, const char *name) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_REGISTER;
    operand.value.physical.reg = ext_reg(number);
    operand.value.physical.access = access;
    operand.asm_constraint = (char *)constraint;
    operand.asm_name = (char *)name;
    return operand;
}

static MinicMachineOperand ext_mem(unsigned int base,
    const char *constraint, const char *name) {
    MinicMachineOperand operand;
    (void)memset(&operand, 0, sizeof(operand));
    operand.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    operand.value.address.target_mode = MINIC_LOONG_ADDRESS_IMM12;
    operand.value.address.has_base = true;
    operand.value.address.base = ext_reg(base);
    operand.value.address.scale = 1U;
    operand.asm_constraint = (char *)constraint;
    operand.asm_name = (char *)name;
    return operand;
}

static MinicMachineOperand ext_immediate(int64_t value,
    const char *constraint) {
    MinicMachineOperand operand = {0};
    operand.kind = MINIC_MACHINE_OPERAND_INTEGER;
    operand.value.integer = value;
    operand.asm_constraint = (char *)constraint;
    return operand;
}

static const MinicCoreType *ext_value_type(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool pointee) {
    MinicCoreTypeId type_id;
    if (fn == NULL || fn->values == NULL || fn->type_context == NULL ||
        value >= fn->value_count) return NULL;
    type_id = fn->values[value].core_type_id;
    if (pointee && !minic_lp64_core_pointer_pointee(
            fn->type_context, type_id, &type_id)) return NULL;
    type_id = minic_core_type_unqualified(fn->type_context, type_id);
    return minic_core_type_get(fn->type_context, type_id);
}

static bool ext_integer(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool pointee, size_t width, int sign) {
    const MinicCoreType *type = ext_value_type(fn, value, pointee);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && !type->value.integer.is_bool &&
        type->value.integer.bit_width == type->size * 8U &&
        (width == 0U || type->size == width) &&
        (sign < 0 || type->value.integer.is_signed == (sign != 0));
}

/* A semantic role check is independent of source-constraint admission.
 * Physical GNU early-clobber and +r/+ZC read/write flags come from Core's
 * sealed per-operand plan, not from the machine template parser. */
static bool ext_roles(const ExtendedSelection *s, size_t count,
    const MinicCoreStructuredInlineAsmOperandKind *kinds,
    const size_t *widths, const int *signs,
    const bool *early, const bool *readwrite,
    size_t tied_input, size_t tied_output) {
    size_t i;
    if (s->instruction->result != MINIC_CORE_VALUE_INVALID ||
        s->instruction->value.structured_inline_asm.operand_count != count)
        return false;
    for (i = 0U; i < count; ++i) {
        const MinicCoreStructuredInlineAsmOperand *r = &s->roles[i];
        const bool pointee = kinds[i] !=
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        const bool tie = i == tied_input;
        if (r->operand_index != i || r->kind != kinds[i] ||
            r->has_fixed_register_binding || r->early_clobber ||
            r->physical_early_clobber != early[i] ||
            r->physical_read_write != readwrite[i] ||
            r->has_tied_output != tie ||
            (tie && r->tied_output_index != tied_output) ||
            !(signs[i] == -2 && widths[i] == 0U && !pointee
                ? minic_core_function_value_is_kind(s->fn,r->value,
                    MINIC_CORE_TYPE_INTEGER)
                : ext_integer(s->fn,r->value,pointee,widths[i],signs[i])))
            return false;
    }
    return true;
}

static bool ext_source(const ExtendedSelection *s, uint32_t form_id,
    uint32_t variant, bool explicit_memory, size_t clobber_count) {
    const MinicCoreInlineAsm *source = s->source;
    if (source == NULL || source->template_text == NULL ||
        source->target_machine_form_id != form_id ||
        source->target_machine_form_variant != variant ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->explicit_memory_clobber != explicit_memory ||
        !source->implicit_memory_effect ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->register_clobber_count != clobber_count ||
        !(form_id <= LOONG_DESC_PTE ?
            minic_loongarch64_typed_descriptor_matches_template(
                form_id,variant,source->template_text,source->template_length) :
            minic_loongarch64_typed_scalar_form_matches_template(
                form_id,variant,source->template_text,source->template_length)))
        return false;
    if (clobber_count != 0U) {
        const MinicCoreInlineAsmRegisterClobber *clobber;
        if (clobber_count != 1U || source->register_clobbers == NULL)
            return false;
        clobber = &source->register_clobbers[0];
        if (clobber->name == NULL || clobber->name_length != 2U ||
            memcmp(clobber->name,"t0",2U) != 0) return false;
    }
    return true;
}

static bool ext_emit(const ExtendedSelection *s,
    const MinicMachineOperand *bindings, size_t count, bool clobbers_t0) {
    MinicMachineRegisterEffect scratch = {0};
    if (clobbers_t0) {
        scratch.reg = ext_reg(EXT_T0);
        scratch.access = MINIC_MACHINE_REGISTER_WRITE |
                         MINIC_MACHINE_REGISTER_CLOBBER;
    }
    return minic_machine_builder_emit_core_source_asm(s->builder,s->source,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        bindings,count,clobbers_t0 ? &scratch : NULL,
        clobbers_t0 ? 1U : 0U,false,false);
}

static bool ext_futex_cmpxchg(const ExtendedSelection *s) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_READWRITE,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    static const size_t widths[] = {4U,4U,4U,4U,4U,4U};
    static const int signs[] = {1,0,0,0,0,0};
    static const bool early[] = {false,true,false,false,false,false};
    static const bool rw[] = {true,false,false,false,false,false};
    const MinicCoreStructuredInlineAsmOperand *b = s->roles;
    MinicMachineOperand ops[6] = {{0}};
    if (!ext_source(s,LOONG_DESC_FUTEX_CMPXCHG,0U,true,1U) ||
        !loong_linux_futex_cmpxchg_form(s->source->template_text,
            s->source->template_length) ||
        !ext_roles(s,6U,kinds,widths,signs,early,rw,SIZE_MAX,0U) ||
        !s->read_lvalue(s->opaque,b[0].value,EXT_T6,4U) ||
        !s->load(s->opaque,b[3].value,EXT_T4,true) ||
        !s->load(s->opaque,b[2].value,EXT_T5,true) ||
        !s->load(s->opaque,b[4].value,EXT_T2,false) ||
        !s->load(s->opaque,b[5].value,EXT_T8,false)) return false;
    ops[0] = ext_gp(EXT_T6,MINIC_MACHINE_REGISTER_READ |
        MINIC_MACHINE_REGISTER_WRITE,"+r",NULL);
    ops[1] = ext_gp(EXT_T1,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    ops[2] = ext_mem(EXT_T5,"=ZC",NULL);
    ops[3] = ext_mem(EXT_T4,"ZC",NULL);
    ops[4] = ext_gp(EXT_T2,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    ops[5] = ext_gp(EXT_T8,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    return ext_emit(s,ops,6U,true) &&
        s->store(s->opaque,b[0].value,EXT_T6,4U) &&
        s->store(s->opaque,b[1].value,EXT_T1,4U);
}

static bool ext_add_unless(const ExtendedSelection *s) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    static const bool early[] = {true,true,false,false,false};
    static const bool rw[] = {false,false,false,false,false};
    static const int signs[] = {-1,-1,-1,-1,-1};
    const unsigned int form = loong_linux_add_unless_form(
        s->source->template_text,s->source->template_length);
    const size_t width = form == 1U ? 4U : 8U;
    const size_t widths[] = {width,width,width,width,width};
    const MinicCoreStructuredInlineAsmOperand *b = s->roles;
    MinicMachineOperand ops[5] = {{0}};
    if ((form != 1U && form != 2U) ||
        !ext_source(s,MINIC_LOONG_MACHINE_ASM_ADD_UNLESS,form,true,0U) ||
        !ext_roles(s,5U,kinds,widths,signs,early,rw,SIZE_MAX,0U) ||
        !s->load(s->opaque,b[2].value,EXT_T2,true) ||
        !s->load(s->opaque,b[3].value,EXT_T4,false) ||
        !s->load(s->opaque,b[4].value,EXT_T5,false)) return false;
    ops[0] = ext_gp(EXT_T0,MINIC_MACHINE_REGISTER_WRITE,"=&r","p");
    ops[1] = ext_gp(EXT_T1,MINIC_MACHINE_REGISTER_WRITE,"=&r","rc");
    ops[2] = ext_mem(EXT_T2,"=ZB","c");
    ops[3] = ext_gp(EXT_T4,MINIC_MACHINE_REGISTER_READ,"r","a");
    ops[4] = ext_gp(EXT_T5,MINIC_MACHINE_REGISTER_READ,"r","u");
    return ext_emit(s,ops,5U,false) &&
        s->store(s->opaque,b[0].value,EXT_T0,width) &&
        s->store(s->opaque,b[1].value,EXT_T1,width);
}

static bool ext_futex_rmw(const ExtendedSelection *s) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    static const size_t widths[] = {4U,4U,4U,4U,4U,0U};
    /* The original sealed futex GNU input accepts any integer C value,
     * including _Bool, and Core owns its scalar width. Sign mode -2 is the
     * exact kind-only check; output and memory roles remain width-specific. */
    static const int signs[] = {1,1,0,1,0,-2};
    static const bool early[] = {false,true,false,false,false,false};
    static const bool rw[] = {false,false,false,false,false,false};
    const LoongLinuxFutexRmw form = loong_linux_futex_rmw_form(
        s->source->template_text,s->source->template_length);
    const MinicCoreStructuredInlineAsmOperand *b = s->roles;
    MinicMachineOperand ops[6] = {{0}};
    MinicCoreInstructionId def_id;
    const MinicCoreInstruction *constant;
    if (form < LOONG_FUTEX_SET || form > LOONG_FUTEX_XOR ||
        !ext_source(s,MINIC_LOONG_MACHINE_ASM_FUTEX_RMW,(uint32_t)form,
            true,1U) ||
        !ext_roles(s,6U,kinds,widths,signs,early,rw,3U,0U) ||
        (def_id = s->fn->values[b[3].value].definition) >=
            s->fn->instruction_count) return false;
    constant = &s->fn->instructions[def_id];
    if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
        constant->result != b[3].value ||
        constant->value.integer_value != 0 ||
        !s->load(s->opaque,b[4].value,EXT_T4,true) ||
        !s->load(s->opaque,b[2].value,EXT_T5,true) ||
        !s->load(s->opaque,b[5].value,EXT_T2,false) ||
        !s->load(s->opaque,b[3].value,EXT_T6,false)) return false;
    ops[0] = ext_gp(EXT_T6,MINIC_MACHINE_REGISTER_WRITE,"=r",NULL);
    ops[1] = ext_gp(EXT_T1,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    ops[2] = ext_mem(EXT_T5,"=ZC",NULL);
    /* GNU matching input "0": Core ties this exact role to output0 and
     * physical selection binds both views to the same T6 register. The
     * machine validator's register constraint syntax is `r`, never the
     * source-language output ordinal `0`. */
    ops[3] = ext_gp(EXT_T6,MINIC_MACHINE_REGISTER_READ,"r",NULL);
    ops[4] = ext_mem(EXT_T4,"ZC",NULL);
    ops[5] = ext_gp(EXT_T2,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    return ext_emit(s,ops,6U,true) &&
        s->store(s->opaque,b[0].value,EXT_T6,4U) &&
        s->store(s->opaque,b[1].value,EXT_T1,4U);
}

static bool ext_sub_positive(const ExtendedSelection *s) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    static const bool early[] = {true,true,false,false};
    static const bool rw[] = {false,false,true,false};
    static const int signs[] = {-1,-1,-1,-1};
    LoongLinuxSubPositive form = {0};
    uint32_t variant;
    MinicMachineOperand ops[4] = {{0}};
    const MinicCoreStructuredInlineAsmOperand *b = s->roles;
    if (!loong_linux_sub_positive_form(s->source->template_text,
            s->source->template_length,&form) ||
        (form.width != 4U && form.width != 8U)) return false;
    variant = (form.width == 8U ? 2U : 0U) +
        (form.add_negated ? 1U : 2U);
    {
        const size_t widths[] = {form.width,form.width,form.width,
                                 form.width};
        if (!ext_source(s,MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE,
                variant,false,0U) ||
            !ext_roles(s,4U,kinds,widths,signs,early,rw,SIZE_MAX,0U) ||
            !s->load(s->opaque,b[2].value,EXT_T4,true)) return false;
    }
    ops[0] = ext_gp(EXT_T1,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    ops[1] = ext_gp(EXT_T2,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    ops[2] = ext_mem(EXT_T4,"+ZC",NULL);
    if (form.add_negated) {
        MinicCoreInstructionId def_id;
        const MinicCoreInstruction *constant;
        int64_t value;
        if ((def_id = s->fn->values[b[3].value].definition) >=
                s->fn->instruction_count) return false;
        constant = &s->fn->instructions[def_id];
        if (constant->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT ||
            constant->result != b[3].value) return false;
        value = constant->value.integer_value;
        /* Core integer constants preserve their source-width bit pattern:
         * a 32-bit source `-1` arrives as 0xffffffff, not sign-extended
         * INT64_C(-1). The LoongArch ADDI encoding consumes the 12-bit
         * signed immediate after source-width interpretation. */
        if (form.width == 4U)
            value = (int64_t)(int32_t)(uint32_t)value;
        if (value < -2048 || value > 2047) return false;
        ops[3] = ext_immediate(value,"I");
    } else {
        if (!s->load(s->opaque,b[3].value,EXT_T5,false)) return false;
        ops[3] = ext_gp(EXT_T5,MINIC_MACHINE_REGISTER_READ,"r",NULL);
    }
    return ext_emit(s,ops,4U,false) &&
        s->store(s->opaque,b[0].value,EXT_T1,form.width) &&
        s->store(s->opaque,b[1].value,EXT_T2,form.width);
}

bool minic_loongarch64_machine_select_llsc_extended(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    bool (*read_lvalue)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    ExtendedSelection selection = {0};
    size_t id;
    if (builder == NULL || builder->function == NULL ||
        ctx == NULL || ctx->fn == NULL || ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        builder->function->target != ctx->target ||
        ins == NULL || load == NULL || store == NULL ||
        read_lvalue == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        (id = ins->value.structured_inline_asm.inline_asm_id) >=
            ctx->fn->inline_asm_count || ctx->fn->inline_asms == NULL)
        return false;
    selection.builder = builder;
    selection.fn = ctx->fn;
    selection.instruction = ins;
    selection.source = &ctx->fn->inline_asms[id];
    selection.roles = ins->value.structured_inline_asm.operands;
    selection.load = load;
    selection.store = store;
    selection.read_lvalue = read_lvalue;
    selection.opaque = opaque;
    switch (selection.source->target_machine_form_id) {
    case LOONG_DESC_FUTEX_CMPXCHG:
        return ext_futex_cmpxchg(&selection);
    case MINIC_LOONG_MACHINE_ASM_ADD_UNLESS:
        return ext_add_unless(&selection);
    case MINIC_LOONG_MACHINE_ASM_FUTEX_RMW:
        return ext_futex_rmw(&selection);
    case MINIC_LOONG_MACHINE_ASM_SUB_POSITIVE:
        return ext_sub_positive(&selection);
    default:
        return false;
    }
}
