#include "target/common/direct_residual_plan.h"
#include "target/common/core_scalar_conversion.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

static const MinicCoreType *residual_type(const MinicCoreFunction *fn,
                                           MinicCoreTypeId id) {
    return fn != NULL && fn->type_context != NULL
        ? minic_core_type_get(fn->type_context,
            minic_core_type_unqualified(fn->type_context, id)) : NULL;
}

static bool residual_kind(const MinicCoreFunction *fn, MinicCoreTypeId id,
                          MinicCoreTypeKind kind) {
    const MinicCoreType *type = residual_type(fn, id);
    return type != NULL && type->kind == kind;
}

/* The only Core integer-conversion admission path for direct LP64.
 * The former block emitters each classified scalar widths/sign, detected
 * binary128 and independently looked up both wide frame slots. */
bool minic_multi_direct_integer_cast_plan(
    const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    const MinicLP64FramePlan *frame, MinicDirectIntegerCastPlan *plan) {
    MinicCoreIntegerConversionShape shape;
    if (module == NULL || target == NULL ||
        frame == NULL || plan == NULL ||
        !minic_core_integer_conversion_shape(fn, ins, &shape))
        return false;
    (void)memset(plan, 0, sizeof(*plan));
    plan->operand = ins->value.operand;
    plan->source_wide = shape.source_bytes == 16U;
    plan->target_wide = shape.target_bytes == 16U;
    plan->target_boolean = shape.target_bool;
    if (plan->source_wide) {
        if (!minic_lp64_frame_plan_wide_slot(
                frame, plan->operand, &plan->source_wide_offset))
            return false;
    } else {
        plan->source_width = shape.source_bytes;
        plan->source_signed = shape.source_signed;
    }
    if (plan->target_wide) {
        if (!minic_lp64_frame_plan_wide_slot(
                frame, ins->result, &plan->target_wide_offset))
            return false;
    } else {
        plan->target_width = shape.target_bytes;
        plan->target_signed = shape.target_signed;
    }
    return true;
}

/* Core constant narrowing depends on target DataLayout, not the int64
 * payload. Unsupported pseudo-bool types preserve the original signed
 * int32 range gate used by both native direct backends. */
bool minic_multi_direct_constant_integer_shape(
    const MinicCoreModule *module, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed) {
    if (module == NULL || target == NULL || fn == NULL ||
        fn->type_context == NULL || ins == NULL ||
        width == NULL || is_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT)
        return false;
    *width = 0U;
    *is_signed = false;
    (void)minic_lp64_core_integer_width(fn->type_context,
                                         ins->core_type_id, width, is_signed);
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
        fn->globals == NULL ||
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
    if (fn == NULL || fn->type_context == NULL || ins == NULL || symbol == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS ||
        id >= fn->function_symbol_count ||
        fn->function_symbols == NULL ||
        !residual_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_POINTER))
        return false;
    *symbol = fn->function_symbols[id].name;
    return *symbol != NULL && (*symbol)[0] != '\0';
}

bool minic_multi_direct_block_address_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreBlockId *block) {
    if (fn == NULL || fn->type_context == NULL || ins == NULL || block == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_BLOCK_ADDRESS ||
        ins->value.block_id >= fn->block_count ||
        ins->result >= fn->value_count ||
        fn->values == NULL ||
        !residual_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_POINTER) ||
        fn->values[ins->result].core_type_id != ins->core_type_id)
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
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
        base == NULL || bytes == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_FIELD_ADDRESS ||
        ins->value.field_address.base >= fn->value_count ||
        fn->values == NULL ||
        !residual_kind(fn,
                       fn->values[ins->value.field_address.base].core_type_id,
                       MINIC_CORE_TYPE_POINTER))
        return false;
    *base = ins->value.field_address.base;
    *bytes = ins->value.field_address.byte_offset;
    return true;
}

bool minic_multi_direct_dynamic_stack_alloc_shape(
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool dynamic_frame) {
    if (fn == NULL || fn->type_context == NULL || ins == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_DYNAMIC_STACK_ALLOC ||
        !dynamic_frame || ins->result >= fn->value_count ||
        ins->value.operand >= fn->value_count ||
        fn->values == NULL ||
        !residual_kind(fn, ins->core_type_id, MINIC_CORE_TYPE_POINTER))
        return false;
    size_t width;
    bool sign;
    return fn->values[ins->result].core_type_id == ins->core_type_id &&
           minic_lp64_core_integer_width(fn->type_context,
               fn->values[ins->value.operand].core_type_id, &width, &sign) &&
           width == 8U && !sign;
}
