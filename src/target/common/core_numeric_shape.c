#include "target/common/core_numeric_shape.h"
#include "target/common/lp64_scalar.h"

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
    return operand < fn->value_count &&
           minic_type_is_integer(ins->type) &&
           minic_type_is_unsigned_integer(fn->values[operand].type) &&
           minic_target_info_integer_width(
               target, program, fn->values[operand].type, width) &&
           (*width == 8U || *width == 16U ||
            *width == 32U || *width == 64U);
}

