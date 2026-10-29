#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool popcount_append_constant(MinicCoreLowerContext *context,
                                     MinicSourceSpan span,
                                     MinicType type,
                                     uint64_t bits,
                                     MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = (int64_t)bits;
    return minic_core_function_append_value_instruction(
        context->function, context->block_id, &instruction, value_id);
}

static bool popcount_append_binary(MinicCoreLowerContext *context,
                                   MinicSourceSpan span,
                                   MinicType type,
                                   MinicCoreInstructionKind kind,
                                   MinicCoreValueId left,
                                   MinicCoreValueId right,
                                   MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = kind;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = left;
    instruction.value.binary.right = right;
    return minic_core_function_append_value_instruction(
        context->function, context->block_id, &instruction, value_id);
}

/* Target-parametrized C scalar operations, not target-machine lowering.
 * Integer promotions/layout enter exclusively through the context target. */
MinicCoreLowerStatus minic_core_lower_try_builtin_unary(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    *handled = false;
    /* Integer population count is target-neutral SWAR.  Unlike clz/ctz,
     * zero is defined (popcount(0)==0).  Keep it in generic Core arithmetic
     * so RV32I and the four 64-bit targets need no POPCNT ISA instruction. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        (expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_POPCOUNT ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_POPCOUNTL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_POPCOUNTLL)) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        MinicCoreValueId x, shift, mask, temporary, low, high;
        MinicCoreLowerStatus status;
        MinicType type;
        unsigned int width;
        uint64_t mask1, mask2, mask4, mask8;
        *handled = true;
        if (operand == NULL || !minic_type_is_unsigned_integer(operand->type) ||
            !minic_type_equal(expression->type, minic_type_int()) ||
            !minic_target_info_integer_width(context->target, context->body->program,
                                             operand->type, &width) ||
            (width != 32U && width != 64U)) return MINIC_CORE_LOWER_UNSUPPORTED;
        type = operand->type;
        mask1 = width == 32U ? UINT32_C(0x55555555) : UINT64_C(0x5555555555555555);
        mask2 = width == 32U ? UINT32_C(0x33333333) : UINT64_C(0x3333333333333333);
        mask4 = width == 32U ? UINT32_C(0x0f0f0f0f) : UINT64_C(0x0f0f0f0f0f0f0f0f);
        mask8 = width == 32U ? UINT32_C(0x01010101) : UINT64_C(0x0101010101010101);
        status = lower_expression(context, expression->value.builtin_unary.operand, &x);
        if (status != MINIC_CORE_LOWER_OK) return status;
#define PC_CONST(bits, output) \
    if (!popcount_append_constant(context, expression->span, type, (bits), &(output))) \
        return MINIC_CORE_LOWER_ERROR
#define PC_BINARY(kind, lhs, rhs, output) \
    if (!popcount_append_binary(context, expression->span, type, (kind), (lhs), (rhs), &(output))) \
        return MINIC_CORE_LOWER_ERROR
        PC_CONST(1U, shift);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT, x, shift, temporary);
        PC_CONST(mask1, mask);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND, temporary, mask, high);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT, x, high, temporary);
        x = temporary;
        PC_CONST(mask2, mask);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND, x, mask, low);
        PC_CONST(2U, shift);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT, x, shift, temporary);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND, temporary, mask, high);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_ADD, low, high, temporary);
        x = temporary;
        PC_CONST(4U, shift);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT, x, shift, high);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_ADD, x, high, temporary);
        PC_CONST(mask4, mask);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND, temporary, mask, x);
        PC_CONST(mask8, mask);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY, x, mask, temporary);
        PC_CONST((uint64_t)(width - 8U), shift);
        PC_BINARY(MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT, temporary, shift, x);
#undef PC_CONST
#undef PC_BINARY
        return minic_core_lower_internal_append_integer_conversion(
            context, expression->span, minic_type_int(), x, value_id);
    }
    /* M129_LEAF_EXPRESSION_OWNERS: __builtin_isdigit is a pure integer leaf.
       Preserve the existing direct-backend contract without target-specific
       instructions: convert once to unsigned int, subtract '0' modulo the
       unsigned width, then compare against 10.  The unsigned range test is
       true exactly for the ten decimal digit codes, including for negative
       int inputs where the subtraction wraps above the range. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CONSTANT_P) {
        MinicConstValue ignored_constant;
        bool is_constant;

        is_constant = core_const_eval_integer_with_locals(
            context, expression->value.builtin_unary.operand, &ignored_constant);
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = is_constant ? 1 : 0;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }

    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_ISDIGIT) {
        const MinicExpression *operand;
        MinicCoreInstruction builtin_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId normalized_value;
        MinicCoreValueId zero_code;
        MinicCoreValueId offset_value;
        MinicCoreValueId digit_count;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(operand->type, minic_type_int()) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        status = minic_core_lower_internal_append_integer_conversion(context,
                                           operand->span,
                                           minic_type_unsigned_int(),
                                           operand_value,
                                           &normalized_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 48;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &zero_code)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = normalized_value;
        builtin_instruction.value.binary.right = zero_code;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &offset_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_unsigned_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 10;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &digit_count)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = offset_value;
        builtin_instruction.value.binary.right = digit_count;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &builtin_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }

    /* M175F_RUNTIME_FFSLL_OWNER: __builtin_ffsll returns a one-based
       least-significant-set-bit index and zero for a zero operand.  Reuse the
       existing unsigned-long CTZ semantic owner instead of introducing a
       target-specific FFS instruction.  RV64's long and long long are both
       64-bit, so converting signed long long to unsigned long preserves the
       operand bit pattern modulo 2^64.  The double zero-test yields a canonical
       int nonzero mask; multiplying by it maps the CTZ zero sentinel (64) back
       to the required ffsll zero result. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_FFSLL) {
        const MinicExpression *operand;
        MinicCoreInstruction builtin_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId unsigned_value;
        MinicCoreValueId trailing_zero_count;
        MinicCoreValueId one;
        MinicCoreValueId one_based_index;
        MinicCoreValueId is_zero;
        MinicCoreValueId is_nonzero;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(operand->type, minic_type_long_long()) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type,
                              minic_type_long_long())) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, operand->span, minic_type_unsigned_long(), operand_value, &unsigned_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CTZ;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.operand = unsigned_value;
        if (!minic_core_function_append_value_instruction(
                context->function,
                context->block_id,
                &builtin_instruction,
                &trailing_zero_count)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.integer_value = 1;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &one)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = trailing_zero_count;
        builtin_instruction.value.binary.right = one;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &one_based_index)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.operand = unsigned_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &is_zero)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.operand = is_zero;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &builtin_instruction, &is_nonzero)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }

        (void)memset(&builtin_instruction, 0, sizeof(builtin_instruction));
        builtin_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
        builtin_instruction.span = expression->span;
        builtin_instruction.type = minic_type_int();
        builtin_instruction.result = MINIC_CORE_VALUE_INVALID;
        builtin_instruction.value.binary.left = one_based_index;
        builtin_instruction.value.binary.right = is_nonzero;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &builtin_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }

    /* SQLite 3.54.1 glibc __bswap_{16,32,64} pressure: lower once to
     * target-neutral integer Core operations. RV64 and x86 both reuse the
     * existing shift/mask/OR semantics; ISA lowering stays target-owned.
     * Do NOT inline the source operand N times: bswap(f(i++)) is evaluated
     * exactly once. The intermediate type is 32 bits for bswap16 and the
     * corresponding unsigned width for bswap32/64. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        (expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_BSWAP16 ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_BSWAP32 ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_BSWAP64)) {
        const MinicExpression *operand;
        MinicBuiltinUnaryOperator opcode;
        MinicType work_type, result_type;
        MinicCoreValueId input, accumulator, term, mask, shift, next_value;
        MinicCoreInstruction bit;
        MinicCoreLowerStatus status;
        size_t byte_count, i;

        opcode = expression->value.builtin_unary.operator_kind;
        result_type = opcode == MINIC_BUILTIN_UNARY_BSWAP16
                          ? minic_type_unsigned_short()
                          : opcode == MINIC_BUILTIN_UNARY_BSWAP32
                                ? minic_type_unsigned_int()
                                : minic_type_unsigned_long_long();
        work_type = opcode == MINIC_BUILTIN_UNARY_BSWAP16
                        ? minic_type_unsigned_int() : result_type;
        byte_count = opcode == MINIC_BUILTIN_UNARY_BSWAP16 ? 2U
                     : opcode == MINIC_BUILTIN_UNARY_BSWAP32 ? 4U : 8U;
        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_equal(expression->type, result_type) ||
            !minic_type_equal(operand->type, result_type))
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        status = lower_expression(context, expression->value.builtin_unary.operand, &input);
        if (status != MINIC_CORE_LOWER_OK) { *handled = true; return status; }
        if (byte_count == 2U) {
            status = minic_core_lower_internal_append_integer_conversion(
                context, expression->span, work_type, input, &input);
            if (status != MINIC_CORE_LOWER_OK) { *handled = true; return status; }
        }
        (void)memset(&bit, 0, sizeof(bit));
        bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        bit.span = expression->span;
        bit.type = work_type;
        bit.result = MINIC_CORE_VALUE_INVALID;
        bit.value.integer_value = 0;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &bit, &accumulator))
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        bit.value.integer_value = 255;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &bit, &mask))
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        for (i = 0U; i < byte_count; ++i) {
            bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            bit.type = work_type;
            bit.value.integer_value = (int64_t)(i * 8U);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit, &shift))
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
            bit.value.binary.left = input;
            bit.value.binary.right = shift;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit, &term))
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            bit.value.binary.left = term;
            bit.value.binary.right = mask;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit, &term))
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            bit.value.integer_value = (int64_t)((byte_count - 1U - i) * 8U);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit, &shift))
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
            bit.value.binary.left = term;
            bit.value.binary.right = shift;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit, &term))
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            bit.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            bit.value.binary.left = accumulator;
            bit.value.binary.right = term;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit, &next_value))
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            accumulator = next_value;
        }
        if (byte_count == 2U) {
            status = minic_core_lower_internal_append_integer_conversion(
                context, expression->span, result_type, accumulator, &accumulator);
            if (status != MINIC_CORE_LOWER_OK) { *handled = true; return status; }
        }
        *value_id = accumulator;
        { *handled = true; return MINIC_CORE_LOWER_OK; }
    }

    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        (expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZ ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZ ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZL ||
         expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CTZLL)) {
        const MinicExpression *operand;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        bool is_clz;

        operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        if (operand == NULL || !minic_type_is_unsigned_integer(operand->type) ||
            !minic_type_equal(expression->type, minic_type_int())) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        status = lower_expression(
            context, expression->value.builtin_unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, operand->type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        is_clz = expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZ ||
                 expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZL ||
                 expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = is_clz ? MINIC_CORE_INSTRUCTION_INTEGER_CLZ
                                  : MINIC_CORE_INSTRUCTION_INTEGER_CTZ;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_comparisons(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    *handled = false;
    /* __builtin_isnan(v) = !(v == v), but the operand is evaluated exactly
     * once, not twice as it would be if we duplicated the AST. Both float
     * and double share our already validated Core binary64 equality path.
     * The float->double conversion preserves NaN classification. */
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_ISNAN) {
        const MinicExpression *operand = minic_c0_program_expression(
            context->body->program, expression->value.builtin_unary.operand);
        MinicCoreValueId source, widened, equal;
        MinicCoreLowerStatus status;
        MinicCoreInstruction compare, test;
        *handled = true;
        if (operand == NULL ||
            (!minic_type_is_float(operand->type) &&
             !minic_type_is_double(operand->type)) ||
            !minic_type_equal(expression->type, minic_type_int()))
            return MINIC_CORE_LOWER_ERROR;
        status = lower_expression(context, expression->value.builtin_unary.operand, &source);
        if (status != MINIC_CORE_LOWER_OK)
            return status;
        widened = source;
        if (minic_type_is_float(operand->type)) {
            (void)memset(&compare, 0, sizeof(compare));
            compare.kind = MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
            compare.span = expression->span;
            compare.type = minic_type_double();
            compare.result = MINIC_CORE_VALUE_INVALID;
            compare.value.operand = source;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &compare, &widened))
                return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&compare, 0, sizeof(compare));
        compare.kind = MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL;
        compare.span = expression->span;
        compare.type = minic_type_int();
        compare.result = MINIC_CORE_VALUE_INVALID;
        compare.value.binary.left = widened;
        compare.value.binary.right = widened;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &compare, &equal))
            return MINIC_CORE_LOWER_ERROR;
        (void)memset(&test, 0, sizeof(test));
        test.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        test.span = expression->span;
        test.type = minic_type_int();
        test.result = MINIC_CORE_VALUE_INVALID;
        test.value.operand = equal;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &test, value_id)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    /* RUNTIME_R0_DOUBLE_COMPARE: equality uses the same binary64
       assignment-conversion seam as arithmetic, so mixed integer/double
       operands are normalized before Core comparison. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicType left_type;
        MinicType right_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (minic_type_is_long_double(left_type) || minic_type_is_long_double(right_type)) {
            MinicCoreInstruction zero_test_instruction;
            MinicCoreValueId compare_value;
            MinicCoreValueId equal_value;
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreLowerStatus status;

            if (!minic_type_equal(expression->type, minic_type_int())) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            status = minic_core_lower_internal_lower_floating_binary_operands(
                context, expression->value.binary.left, expression->value.binary.right,
                minic_type_long_double(), &left, &right);
            if (status != MINIC_CORE_LOWER_OK) { *handled = true; return status; }
            status = minic_core_lower_internal_append_long_double_binary_helper(
                context, expression->span, "__eqtf2", left, right, minic_type_int(),
                &compare_value);
            if (status != MINIC_CORE_LOWER_OK) { *handled = true; return status; }
            (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
            zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            zero_test_instruction.span = expression->span;
            zero_test_instruction.type = minic_type_int();
            zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
            zero_test_instruction.value.operand = compare_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &zero_test_instruction, &equal_value)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
                *value_id = equal_value;
                { *handled = true; return MINIC_CORE_LOWER_OK; }
            }
            zero_test_instruction.value.operand = equal_value;
            { *handled = true; return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &zero_test_instruction, value_id)
                       ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR; }
        }
        if (minic_type_is_double(left_type) || minic_type_is_double(right_type)) {
            MinicCoreInstruction zero_test_instruction;
            MinicCoreValueId equal_value;
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreLowerStatus status;

            if (!minic_type_equal(expression->type, minic_type_int())) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            status = minic_core_lower_internal_lower_floating_binary_operands(context,
                                                  expression->value.binary.left,
                                                  expression->value.binary.right,
                                                  minic_type_double(),
                                                  &left,
                                                  &right);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_EQUAL;
            instruction.span = expression->span;
            instruction.type = minic_type_int();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = left;
            instruction.value.binary.right = right;
            if (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
                { *handled = true; return minic_core_function_append_value_instruction(
                           context->function, context->block_id, &instruction, value_id)
                           ? MINIC_CORE_LOWER_OK
                           : MINIC_CORE_LOWER_ERROR; }
            }
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &equal_value)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
            zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            zero_test_instruction.span = expression->span;
            zero_test_instruction.type = minic_type_int();
            zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
            zero_test_instruction.value.operand = equal_value;
            { *handled = true; return minic_core_function_append_value_instruction(
                       context->function,
                       context->block_id,
                       &zero_test_instruction,
                       value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_EQUAL) {
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        status = minic_core_lower_internal_lower_scalar_equality_operands(
            context, expression->value.binary.left, expression->value.binary.right, &left, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL) {
        MinicCoreInstruction zero_test_instruction;
        MinicCoreValueId equal_value;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        status = minic_core_lower_internal_lower_scalar_equality_operands(
            context, expression->value.binary.left, expression->value.binary.right, &left, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &equal_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        (void)memset(&zero_test_instruction, 0, sizeof(zero_test_instruction));
        zero_test_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        zero_test_instruction.span = expression->span;
        zero_test_instruction.type = minic_type_int();
        zero_test_instruction.result = MINIC_CORE_VALUE_INVALID;
        zero_test_instruction.value.operand = equal_value;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &zero_test_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }
    /* Ordered binary128 comparisons use libgcc's comparison helpers.
       __lttf2 returns a negative value only for an ordered less-than.
       __letf2 returns a positive value for greater-than or unordered; therefore
       !(0 < result) implements ordered <=.  Swap operands for > and >= so no
       NaN-sensitive boolean inversion changes C semantics. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicType left_type;
        MinicType right_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (minic_type_is_long_double(left_type) ||
            minic_type_is_long_double(right_type)) {
            MinicCoreInstruction compare_instruction;
            MinicCoreInstruction zero_instruction;
            MinicCoreInstruction invert_instruction;
            MinicCoreValueId compare_value;
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreValueId zero_value;
            MinicCoreValueId predicate_value;
            MinicCoreLowerStatus status;
            bool inclusive;
            bool swap;

            if (!minic_type_equal(expression->type, minic_type_int())) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            status = minic_core_lower_internal_lower_floating_binary_operands(context,
                                                    expression->value.binary.left,
                                                    expression->value.binary.right,
                                                    minic_type_long_double(),
                                                    &left,
                                                    &right);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            inclusive =
                expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
                expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
            swap =
                expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
                expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
            status = minic_core_lower_internal_append_long_double_binary_helper(context,
                                                      expression->span,
                                                      inclusive ? "__letf2" : "__lttf2",
                                                      swap ? right : left,
                                                      swap ? left : right,
                                                      minic_type_int(),
                                                      &compare_value);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            (void)memset(&zero_instruction, 0, sizeof(zero_instruction));
            zero_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            zero_instruction.span = expression->span;
            zero_instruction.type = minic_type_int();
            zero_instruction.result = MINIC_CORE_VALUE_INVALID;
            zero_instruction.value.integer_value = 0;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &zero_instruction, &zero_value)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&compare_instruction, 0, sizeof(compare_instruction));
            compare_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
            compare_instruction.span = expression->span;
            compare_instruction.type = minic_type_int();
            compare_instruction.result = MINIC_CORE_VALUE_INVALID;
            compare_instruction.value.binary.left = inclusive ? zero_value : compare_value;
            compare_instruction.value.binary.right = inclusive ? compare_value : zero_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &compare_instruction, &predicate_value)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!inclusive) {
                *value_id = predicate_value;
                { *handled = true; return MINIC_CORE_LOWER_OK; }
            }
            (void)memset(&invert_instruction, 0, sizeof(invert_instruction));
            invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            invert_instruction.span = expression->span;
            invert_instruction.type = minic_type_int();
            invert_instruction.result = MINIC_CORE_VALUE_INVALID;
            invert_instruction.value.operand = predicate_value;
            { *handled = true; return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &invert_instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    /* Ordered binary64 relational comparisons must remain direct IEEE-754
       predicates.  <= uses LE; > and >= swap operands.  No inversion is used,
       so unordered NaN inputs remain false as required by C. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicType left_type;
        MinicType right_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (minic_type_is_double(left_type) || minic_type_is_double(right_type)) {
            MinicCoreValueId left;
            MinicCoreValueId right;
            MinicCoreLowerStatus status;
            bool swap;

            if (!minic_type_equal(expression->type, minic_type_int())) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            status = minic_core_lower_internal_lower_floating_binary_operands(context,
                                                  expression->value.binary.left,
                                                  expression->value.binary.right,
                                                  minic_type_double(),
                                                  &left,
                                                  &right);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
                   expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind =
                expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
                        expression->value.binary.operator_kind == MINIC_BINARY_GREATER
                    ? MINIC_CORE_INSTRUCTION_DOUBLE_LESS
                    : MINIC_CORE_INSTRUCTION_DOUBLE_LESS_EQUAL;
            instruction.span = expression->span;
            instruction.type = minic_type_int();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = swap ? right : left;
            instruction.value.binary.right = swap ? left : right;
            { *handled = true; return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreInstruction invert_instruction;
        MinicCoreValueId left;
        MinicCoreValueId less_value;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType left_type;
        MinicType right_type;
        bool invert;
        bool swap;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression != NULL && right_expression != NULL &&
            core_scalar_expression_value_type(context->body, left_expression, &left_type) &&
            core_scalar_expression_value_type(context->body, right_expression, &right_type) &&
            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type)) {
            /* M156_STRUCTURAL_POINTER_RELATIONAL_OWNER: relational
               legality is already decided by frontend/Sema, including GNU void
               pointers and structurally compatible pointer-to-array shapes.
               Core only needs one bit representation for POINTER_LESS.  The
               ordinary conditional-pointer common type remains preferred; if
               side-table identity prevents one despite accepted relational
               compatibility, use the left representation and bitcast both
               operands, matching the established equality owner. */
            if (!minic_c0_pointer_relational_compatible(
                    context->body->program, left_type, right_type)) {
                { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
            }
            if (!minic_type_conditional_pointer_common(
                    left_type, right_type, &common_type)) {
                common_type = left_type;
            }
            status = lower_expression(context, expression->value.binary.left, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            if (left >= context->function->value_count) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!minic_type_equal(context->function->values[left].type, common_type)) {
                status = append_scalar_bitcast(
                    context, left_expression->span, common_type, left, &left);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
            }
            /* M117_BLOCK_LOCAL_POINTER_RELATIONAL: lowering the right operand
               may create a new Core block. Spill the normalized left pointer so
               the eventual POINTER_LESS never references an SSA value owned by
               a predecessor block. */
            MinicCoreObjectId left_object;
            status = minic_core_lower_internal_spill_scalar_value(
                context, left_expression->span, common_type, left, &left_object);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = lower_expression(context, expression->value.binary.right, &right);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            if (right >= context->function->value_count) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!minic_type_equal(context->function->values[right].type, common_type)) {
                status = append_scalar_bitcast(
                    context, right_expression->span, common_type, right, &right);
                if (status != MINIC_CORE_LOWER_OK) {
                    { *handled = true; return status; }
                }
            }
            /* M117_BLOCK_LOCAL_POINTER_RELATIONAL: reload only after the right
               operand is fully lowered and normalized, in the final comparison
               block. */
            status = minic_core_lower_internal_reload_scalar_value(
                context, left_expression->span, common_type, left_object, &left);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
                   expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;
            invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
                     expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_LESS;
            instruction.span = expression->span;
            instruction.type = minic_type_int();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = swap ? right : left;
            instruction.value.binary.right = swap ? left : right;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &less_value)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (!invert) {
                *value_id = less_value;
                { *handled = true; return MINIC_CORE_LOWER_OK; }
            }
            (void)memset(&invert_instruction, 0, sizeof(invert_instruction));
            invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
            invert_instruction.span = expression->span;
            invert_instruction.type = minic_type_int();
            invert_instruction.result = MINIC_CORE_VALUE_INVALID;
            invert_instruction.value.operand = less_value;
            { *handled = true; return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &invert_instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_LESS) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;

        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               common_type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreInstruction invert_instruction;
        MinicCoreValueId left;
        MinicCoreValueId less_value;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        MinicType common_type;
        MinicType left_type;
        MinicType right_type;
        bool invert;
        bool swap;

        if (!minic_type_equal(expression->type, minic_type_int()) || context->target == NULL) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !core_scalar_expression_value_type(context->body, left_expression, &left_type) ||
            !core_scalar_expression_value_type(context->body, right_expression, &right_type) ||
            !minic_type_is_integer(left_type) || !minic_type_is_integer(right_type) ||
            !minic_target_info_integer_common_for_program(
                context->target, context->body->program, left_type, right_type, &common_type)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               common_type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        swap = expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
               expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL;
        invert = expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
                 expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = expression->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = swap ? right : left;
        instruction.value.binary.right = swap ? left : right;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &less_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (!invert) {
            *value_id = less_value;
            { *handled = true; return MINIC_CORE_LOWER_OK; }
        }
        (void)memset(&invert_instruction, 0, sizeof(invert_instruction));
        invert_instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        invert_instruction.span = expression->span;
        invert_instruction.type = minic_type_int();
        invert_instruction.result = MINIC_CORE_VALUE_INVALID;
        invert_instruction.value.operand = less_value;
        { *handled = true; return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &invert_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR; }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_pointer_arithmetic(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    *handled = false;
    /* M93_POINTER_DIFFERENCE: pointer - pointer produces an integer
       element distance. Compose existing target-neutral Core scalar primitives:
       pointer-to-integer bitcasts, byte subtraction, then signed division by
       the semantic pointee stride. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
        minic_type_is_integer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreLowerStatus status;
        MinicCoreObjectId left_object;
        MinicCoreValueId left_pointer;
        MinicCoreValueId right_pointer;
        MinicCoreValueId left_integer;
        MinicCoreValueId right_integer;
        MinicCoreValueId byte_difference;
        MinicCoreValueId divisor;
        MinicType left_type;
        MinicType right_type;
        size_t element_size;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression != NULL && right_expression != NULL &&
            core_scalar_expression_value_type(context->body, left_expression, &left_type) &&
            core_scalar_expression_value_type(context->body, right_expression, &right_type) &&
            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type) &&
            /* BATCH_W_QUALIFIED_POINTER_DIFFERENCE: language compatibility is
               owned by frontend/Sema.  `const T * - T *` is a valid pointer
               difference when the pointed-to object types are compatible;
               Core must not re-impose exact pointer-type equality after Sema
               has accepted the expression.  The representation and stride
               lowering below remain target-neutral and unchanged. */
            minic_c0_pointer_difference_compatible(
                context->body->program, left_type, right_type) &&
            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      core_data_layout(context),
                                                      left_type,
                                                      &element_size) &&
            /* M131_ZERO_STRIDE_POINTER_DIFFERENCE_FAIL_CLOSED: unlike pointer
               +/- integer, pointer difference divides by the pointee stride. */
            element_size != 0U) {
            status = lower_expression(
                context, expression->value.binary.left, &left_pointer);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = append_scalar_bitcast(
                context, left_expression->span, expression->type, left_pointer, &left_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = minic_core_lower_internal_spill_scalar_value(
                context, left_expression->span, expression->type, left_integer, &left_object);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = lower_expression(
                context, expression->value.binary.right, &right_pointer);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = append_scalar_bitcast(
                context, right_expression->span, expression->type, right_pointer, &right_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, left_expression->span, expression->type, left_object, &left_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                { *handled = true; return status; }
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = left_integer;
            instruction.value.binary.right = right_integer;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &byte_difference)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            if (element_size == 1U) {
                *value_id = byte_difference;
                { *handled = true; return MINIC_CORE_LOWER_OK; }
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.integer_value = (int64_t)element_size;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &divisor)) {
                { *handled = true; return MINIC_CORE_LOWER_ERROR; }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = byte_difference;
            instruction.value.binary.right = divisor;
            { *handled = true; return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR; }
        }
    }

    /* M82_BINARY_POINTER_SUBTRACTION: C/GNU pointer +/- integer share the
       same scaled-offset primitive. Subtraction is only valid with the pointer
       on the left; integer - pointer remains fail-closed. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT) &&
        minic_type_is_pointer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *pointer_expression;
        const MinicExpression *right_expression;
        const MinicExpression *index_expression;
        MinicExpressionId pointer_id;
        MinicExpressionId index_id;
        MinicCoreObjectId pointer_object;
        MinicCoreValueId pointer_value;
        MinicCoreValueId index_value;
        MinicCoreValueId offset_value;
        MinicCoreLowerStatus status;
        MinicType expression_value_type;
        MinicType pointer_source_type;
        MinicType pointer_value_type;
        MinicType index_value_type;
        size_t element_size;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        if (minic_type_is_pointer(left_expression->type) &&
            minic_type_is_integer(right_expression->type)) {
            pointer_expression = left_expression;
            index_expression = right_expression;
            pointer_id = expression->value.binary.left;
            index_id = expression->value.binary.right;
        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
            pointer_expression = right_expression;
            index_expression = left_expression;
            pointer_id = expression->value.binary.right;
            index_id = expression->value.binary.left;
        } else {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        /* BATCH_U_POINTER_ARITH_VALUE_TYPES: Core consumes scalar values, not
           lvalue storage qualifiers.  A member reached through `const T *`
           has a const-qualified lvalue type in the semantic AST, but its
           lvalue-to-rvalue result is the unqualified scalar value transported
           by Core.  Use the shared value-type seam for both operands instead
           of comparing emitted values against raw expression storage types. */
        /* M116_POINTER_ARITH_RVALUE_TYPE: pointer arithmetic consumes and
           produces C scalar values.  A nested arithmetic expression may retain
           a top-level qualifier in the semantic AST spelling, but that qualifier
           belongs to the source object, not the transported rvalue.  Canonicalize
           only the pointer operand's top-level qualifier here; pointee qualifiers
           remain part of the pointer type. */
        if (!core_scalar_expression_value_type(
                context->body, pointer_expression, &pointer_source_type) ||
            !minic_type_unqualified(pointer_source_type, &pointer_value_type) ||
            !core_scalar_expression_value_type(
                context->body, index_expression, &index_value_type) ||
            !minic_type_is_pointer(pointer_value_type) ||
            !minic_type_is_integer(index_value_type) ||
            !minic_type_unqualified(expression->type, &expression_value_type) ||
            !minic_type_equal(pointer_value_type, expression_value_type) ||
            !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      core_data_layout(context),
                                                      pointer_value_type,
                                                      &element_size)) {
            { *handled = true; return MINIC_CORE_LOWER_UNSUPPORTED; }
        }
        status = lower_expression(context, pointer_id, &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        status = minic_core_lower_internal_spill_scalar_value(context,
                                    pointer_expression->span,
                                    pointer_value_type,
                                    pointer_value,
                                    &pointer_object);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        status = minic_core_lower_internal_reload_scalar_value(context,
                                     pointer_expression->span,
                                     pointer_value_type,
                                     pointer_object,
                                     &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            { *handled = true; return status; }
        }
        if (pointer_value >= context->function->value_count ||
            index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_value].type,
                              pointer_value_type) ||
            !minic_type_equal(context->function->values[index_value].type,
                              index_value_type)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
        instruction.type = pointer_value_type;
        instruction.value.pointer_offset.base = pointer_value;
        instruction.value.pointer_offset.index = index_value;
        instruction.value.pointer_offset.element_size = element_size;
        /* M116_POINTER_ARITH_RVALUE_TYPE: POINTER_OFFSET already has the
           canonical lvalue-to-rvalue pointer type.  Do not re-attach a top-level
           qualifier merely because the AST keeps that source spelling; nested
           pointer arithmetic and return/assignment conversion consume the value
           type, not the storage-qualified spelling. */
        instruction.value.pointer_offset.subtract =
            expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &offset_value)) {
            { *handled = true; return MINIC_CORE_LOWER_ERROR; }
        }
        *value_id = offset_value;
        { *handled = true; return MINIC_CORE_LOWER_OK; }
    }
    return MINIC_CORE_LOWER_OK;
}

