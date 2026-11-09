#include "target/loongarch64/machine_llsc_select.h"

#include "target/common/core_machine_asm.h"
#include "target/common/lp64_scalar.h"
#include "target/loongarch64/asm_contract_internal.h"
#include "target/loongarch64/machine_asm_form.h"
#include "target/loongarch64/machine_opcode.h"

#include <string.h>

enum {
    LL_T0 = 12U, LL_T1 = 13U, LL_T2 = 14U,
    LL_T4 = 16U, LL_T5 = 17U, LL_T6 = 18U, LL_T7 = 19U
};

static MinicMachineRegister ll_register(unsigned int n) {
    MinicMachineRegister reg = {0};
    reg.number = n;
    reg.width_bits = 64U;
    reg.bank = MINIC_MACHINE_REGISTER_GENERAL;
    return reg;
}

static MinicMachineOperand ll_gp(unsigned int n, unsigned int access,
    const char *constraint, const char *name) {
    MinicMachineOperand result = {0};
    result.kind = MINIC_MACHINE_OPERAND_REGISTER;
    result.value.physical.reg = ll_register(n);
    result.value.physical.access = access;
    result.asm_constraint = (char *)constraint;
    result.asm_name = (char *)name;
    return result;
}

static MinicMachineOperand ll_memory(unsigned int base,
    const char *constraint, const char *name) {
    MinicMachineOperand result;
    (void)memset(&result, 0, sizeof(result));
    result.kind = MINIC_MACHINE_OPERAND_ADDRESS;
    result.value.address.target_mode = MINIC_LOONG_ADDRESS_IMM12;
    result.value.address.has_base = true;
    result.value.address.base = ll_register(base);
    result.value.address.scale = 1U;
    result.asm_constraint = (char *)constraint;
    result.asm_name = (char *)name;
    return result;
}

static bool ll_integer(const MinicCoreFunction *fn,
    MinicCoreValueId value, bool pointee, size_t bytes) {
    MinicCoreTypeId id;
    const MinicCoreType *type;
    if (fn == NULL || fn->type_context == NULL || fn->values == NULL ||
        value >= fn->value_count) return false;
    id = fn->values[value].core_type_id;
    if (pointee && !minic_lp64_core_pointer_pointee(
            fn->type_context, id, &id)) return false;
    id = minic_core_type_unqualified(fn->type_context, id);
    type = minic_core_type_get(fn->type_context, id);
    return type != NULL && type->kind == MINIC_CORE_TYPE_INTEGER &&
        type->has_layout && type->size == bytes &&
        !type->value.integer.is_bool &&
        type->value.integer.bit_width == bytes * 8U;
}

static bool ll_source(const MinicCoreInlineAsm *source, unsigned int id,
    size_t clobbers) {
    if (source == NULL || source->target_machine_form_id != id ||
        source->target_machine_form_variant != 0U ||
        source->operand_plan_owner != MINIC_ASM_OPERAND_PLAN_TARGET_QUALIFIED ||
        !source->is_volatile || source->is_goto ||
        !source->has_memory_clobber ||
        source->register_clobber_count != clobbers ||
        source->resolved_immediate_count != 0U ||
        source->goto_second_target != MINIC_CORE_BLOCK_INVALID ||
        source->template_text == NULL ||
        !minic_loongarch64_typed_descriptor_matches_template(id, 0U,
            source->template_text, source->template_length)) return false;
    if (clobbers != 0U) {
        const MinicCoreInlineAsmRegisterClobber *reg;
        if (clobbers != 1U || source->register_clobbers == NULL)
            return false;
        reg = &source->register_clobbers[0];
        if (reg->name == NULL || reg->name_length != 2U ||
            memcmp(reg->name, "t0", 2U) != 0) return false;
    }
    return true;
}

static bool ll_roles(const MinicCoreFunction *fn,
    const MinicCoreInstruction *ins,
    size_t count, const MinicCoreStructuredInlineAsmOperandKind *kinds,
    const size_t *widths) {
    const MinicCoreStructuredInlineAsmOperand *roles =
        ins->value.structured_inline_asm.operands;
    size_t i;
    if (ins->result != MINIC_CORE_VALUE_INVALID ||
        ins->value.structured_inline_asm.operand_count != count)
        return false;
    for (i = 0U; i < count; ++i) {
        const bool pointee = kinds[i] !=
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT;
        const bool expect_early = kinds[i] ==
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT;
        const bool expect_readwrite = kinds[i] ==
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE;
        if (roles[i].operand_index != i || roles[i].kind != kinds[i] ||
            roles[i].has_fixed_register_binding ||
            roles[i].has_tied_output || roles[i].early_clobber ||
            roles[i].physical_early_clobber != expect_early ||
            roles[i].physical_read_write != expect_readwrite ||
            !ll_integer(fn, roles[i].value, pointee, widths[i]))
            return false;
    }
    return true;
}

static bool ll_cmpxchg(MinicMachineBuilder *builder,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *source,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    const unsigned int form = loong_linux_cmpxchg_form(
        source->template_text, source->template_length);
    const size_t width = form == 2U ? 8U : 4U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicMachineOperand bindings[7] = {{0}};
    MinicMachineRegisterEffect clobber = {0};
    if (form == 0U || form > 3U ||
        !source->explicit_memory_clobber ||
        !ll_source(source, LOONG_DESC_CMPXCHG, form == 3U ? 0U : 1U))
        return false;
    if (form == 3U) {
        static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
        };
        static const size_t widths[] = {4U,4U,4U,4U,4U,4U,4U};
        if (!ll_roles(fn,ins,7U,kinds,widths) ||
            !load(opaque,b[3].value,LL_T4,true) ||
            !load(opaque,b[2].value,LL_T7,true) ||
            !load(opaque,b[4].value,LL_T0,false) ||
            !load(opaque,b[5].value,LL_T1,false) ||
            !load(opaque,b[6].value,LL_T2,false)) return false;
        bindings[0] = ll_gp(LL_T5,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
        bindings[1] = ll_gp(LL_T6,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
        bindings[2] = ll_memory(LL_T7,"=ZC",NULL);
        bindings[3] = ll_memory(LL_T4,"ZC",NULL);
        bindings[4] = ll_gp(LL_T0,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
        bindings[5] = ll_gp(LL_T1,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
        bindings[6] = ll_gp(LL_T2,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
        return minic_machine_builder_emit_core_source_asm(builder,source,
                MINIC_MACHINE_EFFECT_MEMORY_READ |
                    MINIC_MACHINE_EFFECT_MEMORY_WRITE,
                bindings,7U,NULL,0U,false,false) &&
            store(opaque,b[0].value,LL_T5,4U) &&
            store(opaque,b[1].value,LL_T6,4U);
    }
    {
        const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
            MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
            MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
        };
        const size_t widths[] = {width,width,width,width,
                                  form == 1U ? 8U : width};
        /* The sealed 32-bit form permits both 32-bit and long new values. */
        const bool ordinary_width = ll_roles(fn,ins,5U,kinds,widths);
        bool narrow_width = false;
        if (form == 1U && !ordinary_width) {
            const size_t narrow[] = {width,width,width,width,width};
            narrow_width = ll_roles(fn,ins,5U,kinds,narrow);
        }
        if ((!ordinary_width && !narrow_width) ||
            !load(opaque,b[2].value,LL_T4,true) ||
            !load(opaque,b[1].value,LL_T7,true) ||
            !load(opaque,b[3].value,LL_T1,false) ||
            !load(opaque,b[4].value,LL_T2,false)) return false;
    }
    bindings[0] = ll_gp(LL_T5,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    bindings[1] = ll_memory(LL_T7,"=ZB",NULL);
    bindings[2] = ll_memory(LL_T4,"ZB",NULL);
    bindings[3] = ll_gp(LL_T1,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    bindings[4] = ll_gp(LL_T2,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    clobber.reg = ll_register(LL_T0);
    clobber.access = MINIC_MACHINE_REGISTER_WRITE |
                     MINIC_MACHINE_REGISTER_CLOBBER;
    return minic_machine_builder_emit_core_source_asm(builder,source,
        MINIC_MACHINE_EFFECT_MEMORY_READ | MINIC_MACHINE_EFFECT_MEMORY_WRITE,
        bindings,5U,&clobber,1U,false,false) &&
        store(opaque,b[0].value,LL_T5,width);
}

static bool ll_xchg(MinicMachineBuilder *builder,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *source,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    const unsigned int form = loong_linux_xchg_form(
        source->template_text,source->template_length);
    const size_t width = form % 2U == 0U ? 8U : 4U;
    const bool named = form >= 3U && form <= 6U;
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicMachineOperand bindings[3] = {{0}};
    const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_READWRITE,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    const size_t widths[] = {width, named ? 8U : width, width};
    if (form == 0U || form > 16U ||
        source->explicit_memory_clobber == named ||
        !source->implicit_memory_effect ||
        !ll_source(source, LOONG_DESC_XCHG,0U) ||
        !ll_roles(fn,ins,3U,kinds,widths) ||
        !load(opaque,b[0].value,LL_T1,true) ||
        !load(opaque,b[2].value,LL_T0,false)) return false;
    bindings[0] = ll_memory(LL_T1,"+ZB",named ? "mem" : NULL);
    bindings[1] = ll_gp(LL_T2,MINIC_MACHINE_REGISTER_WRITE,"=&r",
                        named ? "tmp" : NULL);
    bindings[2] = ll_gp(LL_T0,MINIC_MACHINE_REGISTER_READ,
                        named || form >= 9U ? "r" : "Jr",
                        named ? "val" : NULL);
    return minic_machine_builder_emit_core_source_asm(builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,3U,NULL,0U,false,false) &&
        store(opaque,b[1].value,LL_T2,named ? 8U : width);
}

static bool ll_xchg_small(MinicMachineBuilder *builder,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicCoreInlineAsm *source,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    static const MinicCoreStructuredInlineAsmOperandKind kinds[] = {
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_REGISTER_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_OUTPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_MEMORY_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT,
        MINIC_CORE_STRUCTURED_INLINE_ASM_SCALAR_INPUT
    };
    static const size_t widths[] = {4U,4U,4U,4U,4U,4U};
    const MinicCoreStructuredInlineAsmOperand *b =
        ins->value.structured_inline_asm.operands;
    MinicMachineOperand bindings[6] = {{0}};
    if (!source->explicit_memory_clobber ||
        !loong_linux_xchg_small_form(source->template_text,
            source->template_length) ||
        !ll_source(source, LOONG_DESC_XCHG_SMALL,0U) ||
        !ll_roles(fn,ins,6U,kinds,widths) ||
        !load(opaque,b[3].value,LL_T4,true) ||
        !load(opaque,b[2].value,LL_T7,true) ||
        !load(opaque,b[4].value,LL_T0,false) ||
        !load(opaque,b[5].value,LL_T1,false)) return false;
    bindings[0] = ll_gp(LL_T5,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    bindings[1] = ll_gp(LL_T6,MINIC_MACHINE_REGISTER_WRITE,"=&r",NULL);
    bindings[2] = ll_memory(LL_T7,"=ZC",NULL);
    bindings[3] = ll_memory(LL_T4,"ZC",NULL);
    bindings[4] = ll_gp(LL_T0,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    bindings[5] = ll_gp(LL_T1,MINIC_MACHINE_REGISTER_READ,"Jr",NULL);
    return minic_machine_builder_emit_core_source_asm(builder,source,
            MINIC_MACHINE_EFFECT_MEMORY_READ |
                MINIC_MACHINE_EFFECT_MEMORY_WRITE,
            bindings,6U,NULL,0U,false,false) &&
        store(opaque,b[0].value,LL_T5,4U) &&
        store(opaque,b[1].value,LL_T6,4U);
}

bool minic_loongarch64_machine_select_llsc(
    MinicMachineBuilder *builder, const MinicDirectLP64BlockContext *ctx,
    const MinicCoreInstruction *ins,
    bool (*load)(void *, MinicCoreValueId, unsigned int, bool),
    bool (*store)(void *, MinicCoreValueId, unsigned int, size_t),
    void *opaque) {
    const MinicCoreFunction *fn;
    const MinicCoreInlineAsm *source;
    size_t id;
    if (builder == NULL || builder->function == NULL ||
        ctx == NULL || (fn = ctx->fn) == NULL ||
        ctx->target == NULL ||
        ctx->target->architecture != MINIC_TARGET_ARCH_LOONGARCH64 ||
        builder->function->target != ctx->target ||
        ins == NULL || load == NULL || store == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_STRUCTURED_INLINE_ASM ||
        (id = ins->value.structured_inline_asm.inline_asm_id) >=
            fn->inline_asm_count || fn->inline_asms == NULL)
        return false;
    source = &fn->inline_asms[id];
    switch (source->target_machine_form_id) {
    case LOONG_DESC_CMPXCHG:
        return ll_cmpxchg(builder,fn,ins,source,load,store,opaque);
    case LOONG_DESC_XCHG:
        return ll_xchg(builder,fn,ins,source,load,store,opaque);
    case LOONG_DESC_XCHG_SMALL:
        return ll_xchg_small(builder,fn,ins,source,load,store,opaque);
    default:
        return false;
    }
}
