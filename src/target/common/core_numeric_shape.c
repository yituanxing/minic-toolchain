#include "target/common/core_numeric_shape.h"
#include "target/common/lp64_scalar.h"

/* Every bound Core function has already passed source-to-Core type admission.
 * These checks consume ONLY its immutable execution types. No target layout
 * or frontend enum/rank table is reopened on the production path. */
static const MinicCoreType *core_numeric_base(const MinicCoreFunction *fn,
                                              MinicCoreTypeId id) {
    if (fn == NULL || fn->type_context == NULL) return NULL;
    id = minic_core_type_unqualified(fn->type_context, id);
    return minic_core_type_get(fn->type_context, id);
}

static bool core_numeric_integer(const MinicCoreFunction *fn,
                                 MinicCoreTypeId id, size_t *width,
                                 bool *sign) {
    return fn != NULL && fn->type_context != NULL &&
           minic_lp64_core_integer_width(fn->type_context, id, width, sign);
}

static bool core_numeric_overflow_result(const MinicCoreFunction *fn,
                                         const MinicCoreInstruction *ins,
                                         MinicCoreTypeId *pointee) {
    MinicCoreValueId address;
    const MinicCoreType *type, *base;
    if (fn == NULL || fn->type_context == NULL || ins == NULL || pointee == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        ins->value.integer_overflow.source_overflow_destination_ineligible ||
        (ins->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
         ins->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
         ins->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
        ins->value.integer_overflow.left >= fn->value_count ||
        ins->value.integer_overflow.right >= fn->value_count ||
        ins->value.integer_overflow.result_address >= fn->value_count)
        return false;
    address = ins->value.integer_overflow.result_address;
    if (!minic_lp64_core_pointer_pointee(fn->type_context,
                                          fn->values[address].core_type_id,
                                          pointee)) return false;
    type = minic_core_type_get(fn->type_context, *pointee);
    base = core_numeric_base(fn, *pointee);
    return type != NULL && !type->is_read_only && !type->is_volatile &&
           base != NULL && base->kind == MINIC_CORE_TYPE_INTEGER &&
           !base->value.integer.is_bool;
}

/* Common Core numeric width/shape proof shared by x86-64, RV64 and the
 * direct LP64 pair. Physical carry/flags and register policy stay target-owned.
 * Retain the previously validated integer width policy without changes. */
/* One checked overflow operator/value/pointee contract for GNU overflow
 * builtins. These are shared Core semantics rather than native flags:
 * AArch64 uses NZCV, LoongArch64 constructs carry/overflow with integer ops. */
bool minic_core_integer_overflow_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    size_t *width, bool *is_signed) {
    MinicCoreValueId left, right, address;
    MinicType value_type;
    int op;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        width == NULL || is_signed == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW)
        return false;
    left = ins->value.integer_overflow.left;
    right = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    op = ins->value.integer_overflow.operator_kind;
    if (fn->type_context != NULL) {
        MinicCoreTypeId result_id;
        if (!core_numeric_overflow_result(fn, ins, &result_id) ||
            result_id != fn->values[left].core_type_id ||
            result_id != fn->values[right].core_type_id ||
            !core_numeric_integer(fn, result_id, width, is_signed)) return false;
        return *width == 4U || *width == 8U;
    }
    return left < fn->value_count && right < fn->value_count &&
           address < fn->value_count &&
           (op == MINIC_CORE_INTEGER_OVERFLOW_ADD ||
            op == MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT ||
            op == MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) &&
           minic_type_pointee(fn->values[address].type, &value_type) &&
           minic_type_is_integer(value_type) &&
           !minic_type_is_bool_integer(value_type) &&
           !minic_type_is_const(value_type) &&
           !minic_type_is_volatile(value_type) &&
           minic_type_equal(value_type, fn->values[left].type) &&
           minic_type_equal(value_type, fn->values[right].type) &&
           minic_lp64_scalar_integer_width(
               program, target, value_type, width, is_signed) &&
           (*width == 4U || *width == 8U);
}

bool minic_core_integer_overflow_mixed_add_i32_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *signed_left) {
    MinicCoreValueId left, right, address;
    MinicType result_type;
    size_t left_width, right_width, result_width;
    bool left_sign, right_sign, result_sign;
    if (program == NULL || target == NULL || fn == NULL || ins == NULL ||
        signed_left == NULL ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        ins->value.integer_overflow.operator_kind != MINIC_CORE_INTEGER_OVERFLOW_ADD)
        return false;
    left = ins->value.integer_overflow.left;
    right = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    if (fn->type_context != NULL) {
        MinicCoreTypeId result_id;
        if (!core_numeric_overflow_result(fn, ins, &result_id) ||
            !core_numeric_integer(fn, result_id, &result_width, &result_sign) ||
            !core_numeric_integer(fn, fn->values[left].core_type_id,
                                  &left_width, &left_sign) ||
            !core_numeric_integer(fn, fn->values[right].core_type_id,
                                  &right_width, &right_sign) ||
            result_width != 4U || !result_sign) return false;
        if (left_width == 4U && left_sign && right_width == 8U && !right_sign) {
            *signed_left = true;
            return true;
        }
        if (left_width == 8U && !left_sign && right_width == 4U && right_sign) {
            *signed_left = false;
            return true;
        }
        return false;
    }
    if (left >= fn->value_count || right >= fn->value_count ||
        address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &result_type) ||
        !minic_type_is_integer(result_type) ||
        minic_type_is_bool_integer(result_type) ||
        minic_type_is_const(result_type) ||
        minic_type_is_volatile(result_type) ||
        !minic_lp64_scalar_integer_width(program, target, result_type,
                                         &result_width, &result_sign) ||
        !minic_lp64_scalar_integer_width(program, target,
                                         fn->values[left].type,
                                         &left_width, &left_sign) ||
        !minic_lp64_scalar_integer_width(program, target,
                                         fn->values[right].type,
                                         &right_width, &right_sign) ||
        result_width != 4U || !result_sign)
        return false;
    if (left_width == 4U && left_sign &&
        right_width == 8U && !right_sign) {
        *signed_left = true;
        return true;
    }
    if (left_width == 8U && !left_sign &&
        right_width == 4U && right_sign) {
        *signed_left = false;
        return true;
    }
    return false;
}

bool minic_core_integer_overflow_u64_u32_to_i32_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *wide_left) {
    MinicCoreValueId left, right, address;
    MinicType result_type;
    size_t left_width, right_width, result_width;
    bool left_sign, right_sign, result_sign;
    if (!program || !target || !fn || !ins || !wide_left ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        ins->value.integer_overflow.operator_kind !=
            MINIC_CORE_INTEGER_OVERFLOW_ADD)
        return false;
    left = ins->value.integer_overflow.left;
    right = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    if (fn->type_context != NULL) {
        MinicCoreTypeId result_id;
        if (!core_numeric_overflow_result(fn, ins, &result_id) ||
            !core_numeric_integer(fn, result_id, &result_width, &result_sign) ||
            !core_numeric_integer(fn, fn->values[left].core_type_id,
                                  &left_width, &left_sign) ||
            !core_numeric_integer(fn, fn->values[right].core_type_id,
                                  &right_width, &right_sign) ||
            result_width != 4U || !result_sign || left_sign || right_sign)
            return false;
        if (left_width == 8U && right_width == 4U) { *wide_left = true; return true; }
        if (left_width == 4U && right_width == 8U) { *wide_left = false; return true; }
        return false;
    }
    if (left >= fn->value_count || right >= fn->value_count ||
        address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &result_type) ||
        !minic_type_is_integer(result_type) ||
        minic_type_is_bool_integer(result_type) ||
        minic_type_is_const(result_type) ||
        minic_type_is_volatile(result_type) ||
        !minic_lp64_scalar_integer_width(program, target, result_type,
                                         &result_width, &result_sign) ||
        !minic_lp64_scalar_integer_width(program, target,
                                         fn->values[left].type,
                                         &left_width, &left_sign) ||
        !minic_lp64_scalar_integer_width(program, target,
                                         fn->values[right].type,
                                         &right_width, &right_sign) ||
        result_width != 4U || !result_sign || left_sign || right_sign)
        return false;
    if (left_width == 8U && right_width == 4U) {
        *wide_left = true;
        return true;
    }
    if (left_width == 4U && right_width == 8U) {
        *wide_left = false;
        return true;
    }
    return false;
}

bool minic_core_integer_overflow_mixed_multiply_i32_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    bool *signed_left, bool *signed_right, bool *signed_result) {
    MinicCoreValueId lhs, rhs, address;
    MinicType result_type;
    size_t left_width, right_width, result_width;
    if (!program || !target || !fn || !ins || !signed_left ||
        !signed_right || !signed_result ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW ||
        ins->value.integer_overflow.operator_kind !=
            MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY)
        return false;
    lhs = ins->value.integer_overflow.left;
    rhs = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    if (fn->type_context != NULL) {
        MinicCoreTypeId result_id;
        if (!core_numeric_overflow_result(fn, ins, &result_id) ||
            !core_numeric_integer(fn, result_id, &result_width, signed_result) ||
            !core_numeric_integer(fn, fn->values[lhs].core_type_id,
                                  &left_width, signed_left) ||
            !core_numeric_integer(fn, fn->values[rhs].core_type_id,
                                  &right_width, signed_right) ||
            left_width != 4U || right_width != 4U || result_width != 4U)
            return false;
        return *signed_left != *signed_right || *signed_left != *signed_result;
    }
    if (lhs >= fn->value_count || rhs >= fn->value_count ||
        address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &result_type) ||
        !minic_type_is_integer(result_type) ||
        minic_type_is_bool_integer(result_type) ||
        minic_type_is_const(result_type) ||
        minic_type_is_volatile(result_type) ||
        !minic_lp64_scalar_integer_width(program, target, result_type,
                                         &result_width, signed_result) ||
        !minic_lp64_scalar_integer_width(program, target,
                                         fn->values[lhs].type,
                                         &left_width, signed_left) ||
        !minic_lp64_scalar_integer_width(program, target,
                                         fn->values[rhs].type,
                                         &right_width, signed_right) ||
        left_width != 4U || right_width != 4U || result_width != 4U)
        return false;
    /* Common is only classifying an otherwise unsupported mixed ABI shape.
     * Uniform types are already governed by the original shape validator. */
    return *signed_left != *signed_right || *signed_left != *signed_result;
}

bool minic_core_integer_overflow_narrow_result_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    MinicCoreIntegerOverflowNarrowShape *shape) {
    MinicCoreValueId lhs, rhs, address;
    MinicType result_type;
    int op;
    if (!program || !target || !fn || !ins || !shape ||
        ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_OVERFLOW)
        return false;
    op = ins->value.integer_overflow.operator_kind;
    lhs = ins->value.integer_overflow.left;
    rhs = ins->value.integer_overflow.right;
    address = ins->value.integer_overflow.result_address;
    if (fn->type_context != NULL) {
        MinicCoreTypeId result_id;
        /* The shared Core overflow contract already excludes source-only
         * ineligible destinations, including enum representation aliases. */
        if (address >= fn->value_count ||
            !core_numeric_overflow_result(fn, ins, &result_id) ||
            !core_numeric_integer(fn, result_id, &shape->result_width,
                                  &shape->result_signed) ||
            !core_numeric_integer(fn, fn->values[lhs].core_type_id,
                                  &shape->left_width, &shape->left_signed) ||
            !core_numeric_integer(fn, fn->values[rhs].core_type_id,
                                  &shape->right_width, &shape->right_signed))
            return false;
        return (shape->result_width == 1U || shape->result_width == 2U) &&
               shape->left_width >= 1U && shape->left_width <= 4U &&
               shape->right_width >= 1U && shape->right_width <= 4U;
    }
    if ((op != MINIC_CORE_INTEGER_OVERFLOW_ADD &&
         op != MINIC_CORE_INTEGER_OVERFLOW_SUBTRACT &&
         op != MINIC_CORE_INTEGER_OVERFLOW_MULTIPLY) ||
        lhs >= fn->value_count || rhs >= fn->value_count ||
        address >= fn->value_count ||
        !minic_type_pointee(fn->values[address].type, &result_type) ||
        !minic_type_is_integer(result_type) ||
        minic_type_is_enum(result_type) ||
        minic_type_is_bool_integer(result_type) ||
        minic_type_is_const(result_type) ||
        minic_type_is_volatile(result_type) ||
        !minic_lp64_scalar_integer_width(
            program, target, result_type,
            &shape->result_width, &shape->result_signed) ||
        !minic_lp64_scalar_integer_width(
            program, target, fn->values[lhs].type,
            &shape->left_width, &shape->left_signed) ||
        !minic_lp64_scalar_integer_width(
            program, target, fn->values[rhs].type,
            &shape->right_width, &shape->right_signed))
        return false;
    return (shape->result_width == 1U || shape->result_width == 2U) &&
           shape->left_width >= 1U && shape->left_width <= 4U &&
           shape->right_width >= 1U && shape->right_width <= 4U;
}

/* CLZ/CTZ are defined here for unsigned 8/16/32/64 Core operands.
 * The ISA owns masking or bitfield extraction and exact machine opcodes. */
bool minic_core_integer_bitscan_shape(
    const MinicC0Program *program, const MinicTargetInfo *target,
    const MinicCoreFunction *fn, const MinicCoreInstruction *ins,
    unsigned int *width) {
    MinicCoreValueId operand;
    if (program == NULL || target == NULL || fn == NULL ||
        ins == NULL || width == NULL ||
        (ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CLZ &&
         ins->kind != MINIC_CORE_INSTRUCTION_INTEGER_CTZ))
        return false;
    operand = ins->value.operand;
    if (fn->type_context != NULL) {
        const MinicCoreType *operand_type;
        if (operand >= fn->value_count || fn->values == NULL ||
            !minic_lp64_core_kind(fn->type_context, ins->core_type_id,
                                  MINIC_CORE_TYPE_INTEGER)) return false;
        operand_type = core_numeric_base(fn, fn->values[operand].core_type_id);
        if (operand_type == NULL || operand_type->kind != MINIC_CORE_TYPE_INTEGER ||
            operand_type->value.integer.is_signed ||
            (operand_type->value.integer.bit_width != 8U &&
             operand_type->value.integer.bit_width != 16U &&
             operand_type->value.integer.bit_width != 32U &&
             operand_type->value.integer.bit_width != 64U)) return false;
        *width = operand_type->value.integer.bit_width;
        return true;
    }
    return operand < fn->value_count &&
           minic_type_is_integer(ins->type) &&
           minic_type_is_unsigned_integer(fn->values[operand].type) &&
           minic_target_info_integer_width(
               target, program, fn->values[operand].type, width) &&
           (*width == 8U || *width == 16U ||
            *width == 32U || *width == 64U);
}

