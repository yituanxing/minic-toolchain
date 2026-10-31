#include "target/common/direct_residual_plan.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

/* The only Core integer-conversion admission path for direct LP64.
 * The former block emitters each classified scalar widths/sign, detected
 * binary128 and independently looked up both wide frame slots. */
bool minic_multi_direct_integer_cast_plan(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame, MinicDirectIntegerCastPlan *plan) {
    MinicType source;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        frame == NULL || plan == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONVERSION ||
        ins->value.operand >= fn->value_count)
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    plan->operand = ins->value.operand;
    source = fn->values[plan->operand].type;
    plan->source_wide = minic_type_is_int128_integer(source);
    plan->target_wide = minic_type_is_int128_integer(ins->type);
    plan->target_boolean = minic_type_is_bool_integer(ins->type);
    if (plan->source_wide) {
        if (!minic_lp64_frame_plan_wide_slot(
                frame, plan->operand, &plan->source_wide_offset))
            return false;
    } else if (!minic_lp64_scalar_integer_width(
                   program, target, source, &plan->source_width,
                   &plan->source_signed)) {
        return false;
    }
    if (plan->target_wide) {
        if (!minic_lp64_frame_plan_wide_slot(
                frame, ins->result, &plan->target_wide_offset))
            return false;
    } else if (!minic_lp64_scalar_integer_width(
                   program, target, ins->type, &plan->target_width,
                   &plan->target_signed)) {
        return false;
    }
    return true;
}

/* Core constant narrowing depends on target DataLayout, not the int64
 * payload. Unsupported pseudo-bool types preserve the original signed
 * int32 range gate used by both native direct backends. */
bool minic_multi_direct_constant_integer_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreInstruction *ins, size_t *width, bool *is_signed) {
    if (program == NULL || target == NULL || ins == NULL ||
        width == NULL || is_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT)
        return false;
    *width = 0U;
    *is_signed = false;
    (void)minic_lp64_scalar_integer_width(
        program, target, ins->type, width, is_signed);
    return *width == 8U || *width != 0U ||
           (ins->value.integer_value >= INT32_MIN &&
            ins->value.integer_value <= INT32_MAX);
}

bool minic_multi_direct_global_address_symbol(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const char **symbol) {
    if (fn == NULL || ins == NULL || symbol == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_GLOBAL_ADDRESS ||
        ins->value.global_id >= fn->global_count ||
        fn->globals[ins->value.global_id].name == NULL)
        return false;
    *symbol = fn->globals[ins->value.global_id].name;
    return true;
}

bool minic_multi_direct_function_address_symbol(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const char **symbol) {
    const MinicCoreFunctionSymbolId id = ins != NULL
        ? ins->value.function_symbol_id : 0U;
    if (fn == NULL || ins == NULL || symbol == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
        id >= fn->function_symbol_count ||
        !minic_type_is_pointer(ins->type))
        return false;
    *symbol = fn->function_symbols[id].name;
    return *symbol != NULL && (*symbol)[0] != '\0';
}

bool minic_multi_direct_block_address_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreBlockId *block) {
    if (fn == NULL || ins == NULL || block == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS ||
        ins->value.block_id >= fn->block_count ||
        ins->result >= fn->value_count ||
        !minic_type_is_pointer(ins->type) ||
        !minic_type_equal(fn->values[ins->result].type, ins->type))
        return false;
    *block = ins->value.block_id;
    return true;
}

bool minic_multi_direct_object_address_location(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame, size_t *offset) {
    return fn != NULL && ins != NULL && frame != NULL && offset != NULL &&
        ins->kind == MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS &&
        ins->value.object_id < fn->object_count &&
        minic_lp64_frame_plan_object(
            frame, (size_t)ins->value.object_id, offset);
}

bool minic_multi_direct_field_address_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreValueId *base, size_t *bytes) {
    if (fn == NULL || ins == NULL || base == NULL || bytes == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_FIELD_ADDRESS ||
        ins->value.field_address.base >= fn->value_count ||
        !minic_type_is_pointer(
            fn->values[ins->value.field_address.base].type))
        return false;
    *base = ins->value.field_address.base;
    *bytes = ins->value.field_address.byte_offset;
    return true;
}

bool minic_multi_direct_dynamic_stack_alloc_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool dynamic_frame) {
    if (fn == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC ||
        !dynamic_frame || ins->result >= fn->value_count ||
        ins->value.operand >= fn->value_count ||
        !minic_type_is_pointer(ins->type))
        return false;
    return minic_type_equal(fn->values[ins->result].type, ins->type) &&
        minic_type_equal(fn->values[ins->value.operand].type,
                         minic_type_unsigned_long());
}
