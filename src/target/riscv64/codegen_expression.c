#include "target/riscv64/codegen_internal.h"
#include "target/data_layout.h"
#include "target/riscv64/abi.h"
#include "target/riscv64/layout.h"
#include "target/target_info.h"

#include <inttypes.h>
#include <string.h>

static bool minic_riscv64_emit_expression_impl(FILE *file,
                                               const MinicC0Program *program,
                                               const MinicFunction *function,
                                               const MinicRiscv64FunctionLayout *function_layout,
                                               MinicExpressionId expression_id,
                                               size_t record_result_temporary_size);

static bool minic_riscv64_pointer_element_size(const MinicC0Program *program,
                                               MinicType pointer_type,
                                               size_t *element_size) {
    MinicType pointee;
    size_t element_alignment;

    if (element_size == NULL || !minic_type_pointee(pointer_type, &pointee)) {
        return false;
    }
    if (minic_type_is_void(pointee) || minic_type_is_function(pointee)) {
        *element_size = 1U;
        return true;
    }
    return minic_riscv64_type_layout(program, pointee, element_size, &element_alignment);
}

static bool
minic_riscv64_emit_normalize_integer(FILE *file, MinicType type, const char *register_name) {
    return minic_riscv64_emit_integer_conversion(file, type, register_name);
}

static bool minic_riscv64_emit_variadic_argument_conversion(FILE *file, MinicType type) {
    if (minic_type_is_pointer(type) || minic_type_is_double(type)) {
        return true;
    }
    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_long_integer(type)) {
        return true;
    }
    return fprintf(file, "  addiw a0, a0, 0\n") >= 0;
}

static bool minic_riscv64_emit_integer_result_conversion(FILE *file,
                                                         MinicType operation_type,
                                                         MinicType result_type,
                                                         const char *register_name) {
    if (!minic_riscv64_emit_integer_conversion(file, operation_type, register_name)) {
        return false;
    }
    return minic_type_equal(operation_type, result_type) ||
           minic_riscv64_emit_integer_conversion(file, result_type, register_name);
}

static const char *minic_riscv64_integer_to_double_instruction(MinicType type) {
    if (!minic_type_is_integer(type)) {
        return NULL;
    }
    if (minic_type_is_long_integer(type)) {
        return minic_type_is_unsigned_integer(type) ? "fcvt.d.lu" : "fcvt.d.l";
    }
    return minic_type_is_unsigned_integer(type) ? "fcvt.d.wu" : "fcvt.d.w";
}

static bool minic_riscv64_emit_integer_to_double(FILE *file,
                                                 MinicType type,
                                                 const char *int_reg,
                                                 const char *fp_reg) {
    const char *instruction;

    instruction = minic_riscv64_integer_to_double_instruction(type);
    if (instruction == NULL) {
        return false;
    }
    return fprintf(file, "  %s %s, %s\n", instruction, fp_reg, int_reg) >= 0;
}

static bool type_is_condition_scalar(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool minic_riscv64_emit_logical_binary(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              const MinicRiscv64FunctionLayout *function_layout,
                                              const MinicExpression *expression,
                                              MinicExpressionId expression_id) {
    const MinicExpression *left;
    const MinicExpression *right;
    MinicBinaryOperator operator_kind;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_BINARY ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    operator_kind = expression->value.binary.operator_kind;
    if (operator_kind != MINIC_BINARY_LOGICAL_AND && operator_kind != MINIC_BINARY_LOGICAL_OR) {
        return false;
    }
    left = minic_c0_program_expression(program, expression->value.binary.left);
    right = minic_c0_program_expression(program, expression->value.binary.right);
    if (left == NULL || right == NULL || !type_is_condition_scalar(left->type) ||
        !type_is_condition_scalar(right->type) ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.binary.left)) {
        return false;
    }

    if (operator_kind == MINIC_BINARY_LOGICAL_AND) {
        return fprintf(file, "  beqz a0, .Lminic_logic_false_%zu\n", expression_id) >= 0 &&
               minic_riscv64_emit_expression(
                   file, program, function, function_layout, expression->value.binary.right) &&
               fprintf(file,
                       "  snez a0, a0\n"
                       "  j .Lminic_logic_end_%zu\n"
                       ".Lminic_logic_false_%zu:\n"
                       "  li a0, 0\n"
                       ".Lminic_logic_end_%zu:\n",
                       expression_id,
                       expression_id,
                       expression_id) >= 0;
    }

    return fprintf(file, "  bnez a0, .Lminic_logic_true_%zu\n", expression_id) >= 0 &&
           minic_riscv64_emit_expression(
               file, program, function, function_layout, expression->value.binary.right) &&
           fprintf(file,
                   "  snez a0, a0\n"
                   "  j .Lminic_logic_end_%zu\n"
                   ".Lminic_logic_true_%zu:\n"
                   "  li a0, 1\n"
                   ".Lminic_logic_end_%zu:\n",
                   expression_id,
                   expression_id,
                   expression_id) >= 0;
}

static bool minic_riscv64_emit_double_binary(FILE *file,
                                             MinicBinaryOperator operator_kind,
                                             MinicType left_type,
                                             MinicType right_type) {
    const char *instruction;

    switch (operator_kind) {
    case MINIC_BINARY_ADD:
        instruction = "fadd.d";
        break;
    case MINIC_BINARY_SUBTRACT:
        instruction = "fsub.d";
        break;
    case MINIC_BINARY_MULTIPLY:
        instruction = "fmul.d";
        break;
    case MINIC_BINARY_DIVIDE:
        instruction = "fdiv.d";
        break;
    default:
        return false;
    }

    if (minic_type_is_double(left_type)) {
        if (fprintf(file, "  fmv.d.x ft0, t0\n") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_integer_to_double(file, left_type, "t0", "ft0")) {
        return false;
    }
    if (minic_type_is_double(right_type)) {
        if (fprintf(file, "  fmv.d.x ft1, a0\n") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_integer_to_double(file, right_type, "a0", "ft1")) {
        return false;
    }
    return fprintf(file,
                   "  %s ft0, ft0, ft1\n"
                   "  fmv.x.d a0, ft0\n",
                   instruction) >= 0;
}

static bool minic_riscv64_emit_double_comparison(FILE *file,
                                                 MinicBinaryOperator operator_kind,
                                                 MinicType left_type,
                                                 MinicType right_type) {
    if (minic_type_is_double(left_type)) {
        if (fprintf(file, "  fmv.d.x ft0, t0\n") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_integer_to_double(file, left_type, "t0", "ft0")) {
        return false;
    }
    if (minic_type_is_double(right_type)) {
        if (fprintf(file, "  fmv.d.x ft1, a0\n") < 0) {
            return false;
        }
    } else if (!minic_riscv64_emit_integer_to_double(file, right_type, "a0", "ft1")) {
        return false;
    }

    switch (operator_kind) {
    case MINIC_BINARY_EQUAL:
        return fprintf(file, "  feq.d a0, ft0, ft1\n") >= 0;
    case MINIC_BINARY_NOT_EQUAL:
        return fprintf(file, "  feq.d a0, ft0, ft1\n  xori a0, a0, 1\n") >= 0;
    case MINIC_BINARY_LESS:
        return fprintf(file, "  flt.d a0, ft0, ft1\n") >= 0;
    case MINIC_BINARY_LESS_EQUAL:
        return fprintf(file, "  fle.d a0, ft0, ft1\n") >= 0;
    case MINIC_BINARY_GREATER:
        return fprintf(file, "  flt.d a0, ft1, ft0\n") >= 0;
    case MINIC_BINARY_GREATER_EQUAL:
        return fprintf(file, "  fle.d a0, ft1, ft0\n") >= 0;
    default:
        return false;
    }
}

static bool minic_riscv64_emit_conditional_result_conversion(FILE *file,
                                                             MinicType source_type,
                                                             MinicType result_type) {
    MinicType unqualified_source;
    MinicType unqualified_result;

    if (minic_type_equal(source_type, result_type)) {
        return true;
    }
    if (minic_type_unqualified(source_type, &unqualified_source) &&
        minic_type_unqualified(result_type, &unqualified_result) &&
        minic_type_equal(unqualified_source, unqualified_result)) {
        return true;
    }
    if (minic_type_is_pointer(source_type) && minic_type_is_pointer(result_type)) {
        return true;
    }
    if (minic_type_is_integer(source_type) && minic_type_is_integer(result_type)) {
        return minic_riscv64_emit_integer_conversion(file, result_type, "a0");
    }
    if (minic_type_is_integer(source_type) && minic_type_is_double(result_type)) {
        return minic_riscv64_emit_integer_to_double(file, source_type, "a0", "ft0") &&
               fprintf(file, "  fmv.x.d a0, ft0\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_scale_register(FILE *file,
                                              const char *register_name,
                                              const char *scratch_register,
                                              size_t element_size) {
    size_t value;
    unsigned int shift;

    if (file == NULL || register_name == NULL || scratch_register == NULL || element_size == 0U) {
        return false;
    }
    if (element_size == 1U) {
        return true;
    }
    if ((element_size & (element_size - 1U)) == 0U) {
        value = element_size;
        shift = 0U;
        while (value > 1U) {
            value >>= 1U;
            shift += 1U;
        }
        return fprintf(file, "  slli %s, %s, %u\n", register_name, register_name, shift) >= 0;
    }
    return fprintf(file,
                   "  li %s, %zu\n"
                   "  mul %s, %s, %s\n",
                   scratch_register,
                   element_size,
                   register_name,
                   register_name,
                   scratch_register) >= 0;
}

static bool minic_riscv64_emit_bit_field_load_from_address(FILE *file,
                                                           const MinicRecordField *field,
                                                           size_t bit_offset,
                                                           const char *result_register,
                                                           const char *address_register) {
    size_t byte_count;
    size_t index;
    unsigned int shift;

    if (file == NULL || field == NULL || result_register == NULL || address_register == NULL ||
        !field->is_bit_field || field->bit_width == 0U || field->bit_width > 64U ||
        bit_offset >= 8U || bit_offset > SIZE_MAX - field->bit_width) {
        return false;
    }
    byte_count = (bit_offset + field->bit_width + 7U) / 8U;
    if (byte_count == 0U || byte_count > 8U ||
        fprintf(file, "  mv t5, %s\n", address_register) < 0 ||
        fprintf(file, "  li %s, 0\n", result_register) < 0) {
        return false;
    }
    for (index = 0U; index < byte_count; ++index) {
        if (fprintf(file, "  lbu t6, %zu(t5)\n", index) < 0) {
            return false;
        }
        if (index != 0U && fprintf(file, "  slli t6, t6, %zu\n", index * 8U) < 0) {
            return false;
        }
        if (fprintf(file, "  or %s, %s, t6\n", result_register, result_register) < 0) {
            return false;
        }
    }
    if (bit_offset != 0U &&
        fprintf(file, "  srli %s, %s, %zu\n", result_register, result_register, bit_offset) < 0) {
        return false;
    }
    if (field->bit_width == 64U) {
        return true;
    }
    shift = 64U - (unsigned int)field->bit_width;
    if (fprintf(file, "  slli %s, %s, %u\n", result_register, result_register, shift) < 0) {
        return false;
    }
    return fprintf(file,
                   minic_type_is_signed_integer(field->type) &&
                           !minic_type_is_bool_integer(field->type)
                       ? "  srai %s, %s, %u\n"
                       : "  srli %s, %s, %u\n",
                   result_register,
                   result_register,
                   shift) >= 0;
}

static bool minic_riscv64_emit_bit_field_store_to_address(FILE *file,
                                                          const MinicRecordField *field,
                                                          size_t bit_offset,
                                                          const char *value_register,
                                                          const char *address_register) {
    uint64_t value_mask;
    uint64_t positioned_mask;
    size_t byte_count;
    size_t index;
    unsigned int shift;

    if (file == NULL || field == NULL || value_register == NULL || address_register == NULL ||
        !field->is_bit_field || field->bit_width == 0U || field->bit_width > 64U ||
        bit_offset >= 8U || bit_offset > SIZE_MAX - field->bit_width) {
        return false;
    }
    byte_count = (bit_offset + field->bit_width + 7U) / 8U;
    if (byte_count == 0U || byte_count > 8U ||
        fprintf(file, "  mv t5, %s\n  li t2, 0\n", address_register) < 0) {
        return false;
    }
    for (index = 0U; index < byte_count; ++index) {
        if (fprintf(file, "  lbu t6, %zu(t5)\n", index) < 0 ||
            (index != 0U && fprintf(file, "  slli t6, t6, %zu\n", index * 8U) < 0) ||
            fprintf(file, "  or t2, t2, t6\n") < 0) {
            return false;
        }
    }
    if (field->bit_width == 64U) {
        if (bit_offset != 0U || fprintf(file, "  mv t2, %s\n", value_register) < 0) {
            return false;
        }
    } else {
        value_mask = (UINT64_C(1) << field->bit_width) - UINT64_C(1);
        positioned_mask = value_mask << bit_offset;
        if (fprintf(file,
                    "  li t3, %" PRIu64 "\n"
                    "  and t4, %s, t3\n",
                    value_mask,
                    value_register) < 0 ||
            (bit_offset != 0U && fprintf(file, "  slli t4, t4, %zu\n", bit_offset) < 0) ||
            fprintf(file,
                    "  li t3, %" PRIu64 "\n"
                    "  not t3, t3\n"
                    "  and t2, t2, t3\n"
                    "  or t2, t2, t4\n",
                    positioned_mask) < 0) {
            return false;
        }
    }
    for (index = 0U; index < byte_count; ++index) {
        if (index == 0U) {
            if (fprintf(file, "  sb t2, 0(t5)\n") < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  srli t6, t2, %zu\n"
                           "  sb t6, %zu(t5)\n",
                           index * 8U,
                           index) < 0) {
            return false;
        }
    }
    if (field->bit_width == 64U) {
        return true;
    }
    shift = 64U - (unsigned int)field->bit_width;
    if (fprintf(file, "  slli %s, %s, %u\n", value_register, value_register, shift) < 0) {
        return false;
    }
    return fprintf(file,
                   minic_type_is_signed_integer(field->type) &&
                           !minic_type_is_bool_integer(field->type)
                       ? "  srai %s, %s, %u\n"
                       : "  srli %s, %s, %u\n",
                   value_register,
                   value_register,
                   shift) >= 0;
}

static bool minic_riscv64_emit_lvalue_load_from_address(FILE *file,
                                                        const MinicC0Program *program,
                                                        MinicExpressionId expression_id,
                                                        MinicType type,
                                                        const char *result_register,
                                                        const char *address_register) {
    const MinicRecordField *field;
    const MinicExpression *expression;
    const MinicRecord *record;
    size_t bit_offset;
    size_t field_offset;

    expression = minic_c0_program_expression(program, expression_id);
    field = minic_c0_expression_bit_field(program, expression_id);
    record = expression != NULL && expression->kind == MINIC_EXPRESSION_MEMBER
                 ? minic_c0_program_record(program, expression->value.member.record_id)
                 : NULL;
    if (field != NULL) {
        if (record == NULL ||
            !minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   expression->value.member.field_index,
                                                   &field_offset,
                                                   &bit_offset)) {
            return false;
        }
        (void)field_offset;
        return minic_riscv64_emit_bit_field_load_from_address(
            file, field, bit_offset, result_register, address_register);
    }
    return minic_riscv64_emit_scalar_load(file, type, result_register, address_register);
}

static bool minic_riscv64_emit_lvalue_store_to_address(FILE *file,
                                                       const MinicC0Program *program,
                                                       MinicExpressionId expression_id,
                                                       MinicType type,
                                                       const char *value_register,
                                                       const char *address_register) {
    const MinicRecordField *field;
    const MinicExpression *expression;
    const MinicRecord *record;
    size_t bit_offset;
    size_t field_offset;

    expression = minic_c0_program_expression(program, expression_id);
    field = minic_c0_expression_bit_field(program, expression_id);
    record = expression != NULL && expression->kind == MINIC_EXPRESSION_MEMBER
                 ? minic_c0_program_record(program, expression->value.member.record_id)
                 : NULL;
    if (field != NULL) {
        if (record == NULL ||
            !minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   expression->value.member.field_index,
                                                   &field_offset,
                                                   &bit_offset)) {
            return false;
        }
        (void)field_offset;
        return minic_riscv64_emit_bit_field_store_to_address(
            file, field, bit_offset, value_register, address_register);
    }
    return minic_riscv64_emit_scalar_store(file, type, value_register, address_register);
}

static bool minic_riscv64_emit_update(FILE *file,
                                      const MinicC0Program *program,
                                      const MinicFunction *function,
                                      const MinicRiscv64FunctionLayout *function_layout,
                                      const MinicExpression *expression) {
    const MinicExpression *operand;
    size_t element_size;
    bool increment;
    bool prefix;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_UNARY ||
        (expression->value.unary.operator_kind != MINIC_UNARY_POST_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_POST_DECREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_INCREMENT &&
         expression->value.unary.operator_kind != MINIC_UNARY_PRE_DECREMENT)) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.unary.operand);
    if (operand == NULL || operand->value_category != MINIC_VALUE_LVALUE ||
        (!minic_type_is_integer(operand->type) && !minic_type_is_pointer(operand->type))) {
        return false;
    }
    increment = expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
                expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT;
    prefix = expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
             expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT;
    element_size = 1U;
    if (minic_type_is_pointer(operand->type) &&
        !minic_riscv64_pointer_element_size(program, operand->type, &element_size)) {
        return false;
    }

    if (!minic_riscv64_emit_lvalue_address(
            file, program, function, function_layout, expression->value.unary.operand) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_lvalue_load_from_address(
            file, program, expression->value.unary.operand, operand->type, "t0", "a0") ||
        fprintf(file, "  sd t0, 8(sp)\n") < 0) {
        return false;
    }
    if (minic_type_is_pointer(operand->type)) {
        if (element_size <= 2047U) {
            if (fprintf(file,
                        increment ? "  addi t0, t0, %zu\n" : "  addi t0, t0, -%zu\n",
                        element_size) < 0) {
                return false;
            }
        } else if (fprintf(file,
                           "  li t1, %zu\n"
                           "  %s t0, t0, t1\n",
                           element_size,
                           increment ? "add" : "sub") < 0) {
            return false;
        }
    } else if (fprintf(file, increment ? "  addi t0, t0, 1\n" : "  addi t0, t0, -1\n") < 0 ||
               !minic_riscv64_emit_integer_conversion(file, operand->type, "t0")) {
        return false;
    }
    if (fprintf(file, "  ld t1, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_lvalue_store_to_address(
            file, program, expression->value.unary.operand, operand->type, "t0", "t1")) {
        return false;
    }
    return prefix ? fprintf(file, "  mv a0, t0\n  addi sp, sp, 16\n") >= 0
                  : fprintf(file, "  ld a0, 8(sp)\n  addi sp, sp, 16\n") >= 0;
}

static bool minic_riscv64_emit_subscript_address(FILE *file,
                                                 const MinicC0Program *program,
                                                 const MinicFunction *function,
                                                 const MinicRiscv64FunctionLayout *function_layout,
                                                 const MinicExpression *expression) {
    const MinicExpression *base;
    const MinicExpression *index;
    bool base_is_array_object;
    size_t element_size;
    size_t element_alignment;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_SUBSCRIPT) {
        return false;
    }
    base = minic_c0_program_expression(program, expression->value.subscript.base);
    index = minic_c0_program_expression(program, expression->value.subscript.index);
    if (base == NULL || index == NULL || !minic_type_is_integer(index->type)) {
        return false;
    }

    {
        MinicArrayObjectInfo array_info;

        base_is_array_object = minic_c0_expression_array_object_info(program, base, &array_info);
        if (base_is_array_object && !minic_type_equal(array_info.element_type, expression->type)) {
            return false;
        }
    }

    if (base_is_array_object) {
        if (!minic_riscv64_emit_lvalue_address(
                file, program, function, function_layout, expression->value.subscript.base)) {
            return false;
        }
    } else {
        MinicType pointee;

        if (!minic_type_pointee(base->type, &pointee) ||
            !minic_type_equal(pointee, expression->type) ||
            !minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.subscript.base)) {
            return false;
        }
    }

    if (!minic_riscv64_type_layout(program, expression->type, &element_size, &element_alignment)) {
        return false;
    }

    return fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") >= 0 &&
           minic_riscv64_emit_expression(
               file, program, function, function_layout, expression->value.subscript.index) &&
           minic_riscv64_emit_scale_register(file, "a0", "t1", element_size) &&
           fprintf(file,
                   "  ld t0, 0(sp)\n"
                   "  addi sp, sp, 16\n"
                   "  add a0, t0, a0\n") >= 0;
}

static bool minic_riscv64_emit_member_address(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              const MinicRiscv64FunctionLayout *function_layout,
                                              const MinicExpression *expression) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t field_offset;
    MinicType record_type;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_MEMBER) {
        return false;
    }
    base = minic_c0_program_expression(program, expression->value.member.base);
    record = minic_c0_program_record(program, expression->value.member.record_id);
    field = minic_c0_record_field(record, expression->value.member.field_index);
    if (base == NULL || record == NULL || field == NULL ||
        !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
        record_type.record_id != expression->value.member.record_id ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                               program,
                                               record,
                                               expression->value.member.field_index,
                                               &field_offset) ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.member.base)) {
        return false;
    }
    if (field_offset == 0U) {
        return true;
    }
    if (field_offset <= 2047U) {
        return fprintf(file, "  addi a0, a0, %zu\n", field_offset) >= 0;
    }
    return fprintf(file,
                   "  li t0, %zu\n"
                   "  add a0, a0, t0\n",
                   field_offset) >= 0;
}

static bool
minic_riscv64_emit_expression_owned_block(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicRiscv64FunctionLayout *function_layout,
                                          MinicExpressionId expression_id,
                                          MinicBlockId block_id) {
    size_t label_stride;
    size_t label_counter;

    if (file == NULL || program == NULL || block_id >= program->block_count ||
        program->statement_count == SIZE_MAX) {
        return false;
    }
    label_stride = program->statement_count + 1U;
    if (expression_id > (SIZE_MAX - label_stride) / label_stride) {
        return false;
    }
    label_counter = label_stride + expression_id * label_stride;
    return minic_riscv64_emit_block(
        file, program, function, function_layout, block_id, &label_counter);
}

bool minic_riscv64_emit_lvalue_address(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicFunction *function,
                                       const MinicRiscv64FunctionLayout *function_layout,
                                       MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_address(
            file, program, function, function_layout, expression->value.local_id);
    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object != NULL && object->name_length != 0U &&
               fprintf(file, "  la a0, %s\n", object->name) >= 0;
    }
    case MINIC_EXPRESSION_DEREFERENCE:
        return minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.unary.operand);
    case MINIC_EXPRESSION_SUBSCRIPT:
        return minic_riscv64_emit_subscript_address(
            file, program, function, function_layout, expression);
    case MINIC_EXPRESSION_MEMBER:
        return minic_riscv64_emit_member_address(
            file, program, function, function_layout, expression);
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
        return minic_riscv64_emit_expression_owned_block(
                   file,
                   program,
                   function,
                   function_layout,
                   expression_id,
                   expression->value.compound_literal.initializer_block) &&
               minic_riscv64_emit_object_address(file,
                                                 program,
                                                 function,
                                                 function_layout,
                                                 expression->value.compound_literal.local_id);
    default:
        return false;
    }
}

bool minic_riscv64_emit_address_backed_record_value(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicRiscv64FunctionLayout *function_layout,
    MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (!minic_c0_record_value_is_address_backed(program, expression_id)) {
        return false;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->value_category == MINIC_VALUE_LVALUE) {
        return minic_riscv64_emit_lvalue_address(
            file, program, function, function_layout, expression_id);
    }
    return expression->kind == MINIC_EXPRESSION_STATEMENT &&
           minic_riscv64_emit_expression(file, program, function, function_layout, expression_id);
}

static bool
minic_riscv64_emit_record_value_temporary(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicRiscv64FunctionLayout *function_layout,
                                          MinicExpressionId source_id,
                                          size_t storage_size,
                                          size_t temporary_size) {
    const MinicExpression *source;

    source = minic_c0_program_expression(program, source_id);
    if (source == NULL || !minic_type_is_record(source->type) ||
        !minic_c0_record_value_is_copy_source(program, source_id)) {
        return false;
    }
    if (minic_c0_record_value_is_address_backed(program, source_id)) {
        size_t index;

        if (!minic_riscv64_emit_address_backed_record_value(
                file, program, function, function_layout, source_id) ||
            !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
            fprintf(file, "  mv t2, a0\n  mv t3, sp\n") < 0) {
            return false;
        }
        for (index = 0U; index < storage_size; ++index) {
            if (fprintf(file,
                        "  lbu t0, 0(t2)\n"
                        "  sb t0, 0(t3)\n"
                        "  addi t2, t2, 1\n"
                        "  addi t3, t3, 1\n") < 0) {
                return false;
            }
        }
        return true;
    }
    if (source->kind == MINIC_EXPRESSION_CALL) {
        MinicRiscv64AbiValue value;

        if (!minic_riscv64_abi_classify_value(program, source->type, &value) ||
            value.storage_size != storage_size) {
            return false;
        }
        if (value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
            return storage_size > 16U && temporary_size >= storage_size &&
                   (temporary_size & 15U) == 0U &&
                   minic_riscv64_emit_expression_impl(
                       file, program, function, function_layout, source_id, temporary_size);
        }
        if (value.kind != MINIC_RISCV64_ABI_VALUE_AGGREGATE || value.slot_count == 0U ||
            value.slot_count > 2U ||
            !minic_riscv64_emit_expression(file, program, function, function_layout, source_id) ||
            !minic_riscv64_emit_stack_allocate(file, temporary_size) ||
            fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
            (value.slot_count == 2U && fprintf(file, "  sd a1, 8(sp)\n") < 0)) {
            return false;
        }
        return true;
    }
    return false;
}

static bool
minic_riscv64_emit_record_rvalue_member(FILE *file,
                                        const MinicC0Program *program,
                                        const MinicFunction *function,
                                        const MinicRiscv64FunctionLayout *function_layout,
                                        const MinicExpression *expression,
                                        MinicExpressionId expression_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    size_t field_offset;
    size_t storage_size;
    size_t temporary_size;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_MEMBER ||
        expression->value_category != MINIC_VALUE_RVALUE) {
        return false;
    }
    base = minic_c0_program_expression(program, expression->value.member.base);
    record = minic_c0_program_record(program, expression->value.member.record_id);
    field = minic_c0_record_field(record, expression->value.member.field_index);
    if (base == NULL || record == NULL || field == NULL || !record->is_complete ||
        base->value_category != MINIC_VALUE_RVALUE || !minic_type_is_record(base->type) ||
        base->type.record_id != expression->value.member.record_id ||
        !minic_c0_record_value_is_copy_source(program, expression->value.member.base) ||
        field->is_array || minic_type_is_record(field->type) ||
        !minic_riscv64_type_layout(program,
                                   minic_type_record(expression->value.member.record_id),
                                   &storage_size,
                                   &temporary_size) ||
        storage_size == 0U || storage_size > SIZE_MAX - 15U ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                               program,
                                               record,
                                               expression->value.member.field_index,
                                               &field_offset)) {
        return false;
    }
    temporary_size = (storage_size + 15U) & ~(size_t)15U;
    if (!minic_riscv64_emit_record_value_temporary(file,
                                                   program,
                                                   function,
                                                   function_layout,
                                                   expression->value.member.base,
                                                   storage_size,
                                                   temporary_size)) {
        return false;
    }
    if (field_offset == 0U) {
        if (fprintf(file, "  mv a0, sp\n") < 0) {
            return false;
        }
    } else if (field_offset <= 2047U) {
        if (fprintf(file, "  addi a0, sp, %zu\n", field_offset) < 0) {
            return false;
        }
    } else if (fprintf(file,
                       "  li t0, %zu\n"
                       "  add a0, sp, t0\n",
                       field_offset) < 0) {
        return false;
    }
    return minic_riscv64_emit_lvalue_load_from_address(
               file, program, expression_id, expression->type, "a0", "a0") &&
           minic_riscv64_emit_stack_release(file, temporary_size);
}

static bool minic_riscv64_emit_record_temporary_to_address(FILE *file,
                                                           size_t storage_size,
                                                           size_t temporary_size,
                                                           const char *address_register,
                                                           bool preserve_address) {
    size_t index;

    if (file == NULL || address_register == NULL || storage_size == 0U ||
        temporary_size < storage_size ||
        (preserve_address && fprintf(file, "  mv t4, %s\n", address_register) < 0) ||
        fprintf(file, "  mv t2, sp\n  mv t3, %s\n", address_register) < 0) {
        return false;
    }
    for (index = 0U; index < storage_size; ++index) {
        if (fprintf(file,
                    "  lbu t0, 0(t2)\n"
                    "  sb t0, 0(t3)\n"
                    "  addi t2, t2, 1\n"
                    "  addi t3, t3, 1\n") < 0) {
            return false;
        }
    }
    if (!minic_riscv64_emit_stack_release(file, temporary_size)) {
        return false;
    }
    return !preserve_address || fprintf(file, "  mv a0, t4\n") >= 0;
}

bool minic_riscv64_emit_record_return_value(FILE *file,
                                            const MinicC0Program *program,
                                            const MinicFunction *function,
                                            const MinicRiscv64FunctionLayout *function_layout,
                                            MinicExpressionId source_id,
                                            size_t result_pointer_offset) {
    const MinicExpression *source;
    size_t storage_size;
    size_t temporary_size;

    source = minic_c0_program_expression(program, source_id);
    if (source == NULL || !minic_type_is_record(source->type) ||
        !minic_riscv64_type_layout(program, source->type, &storage_size, &temporary_size) ||
        storage_size <= 16U || storage_size > SIZE_MAX - 15U) {
        return false;
    }
    temporary_size = (storage_size + 15U) & ~(size_t)15U;
    return minic_riscv64_emit_record_value_temporary(
               file, program, function, function_layout, source_id, storage_size, temporary_size) &&
           minic_riscv64_emit_s0_load64(file, "t4", result_pointer_offset) &&
           minic_riscv64_emit_record_temporary_to_address(
               file, storage_size, temporary_size, "t4", false);
}

bool minic_riscv64_emit_record_copy_value(FILE *file,
                                          const MinicC0Program *program,
                                          const MinicFunction *function,
                                          const MinicRiscv64FunctionLayout *function_layout,
                                          MinicExpressionId target_id,
                                          MinicExpressionId source_id,
                                          bool preserve_target_address) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    size_t storage_size;
    size_t temporary_size;

    target = minic_c0_program_expression(program, target_id);
    source = minic_c0_program_expression(program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        minic_type_is_const(target->type) || !minic_type_is_record(target->type) ||
        !minic_type_is_record(source->type) || target->type.record_id != source->type.record_id ||
        !minic_c0_record_value_is_copy_source(program, source_id)) {
        return false;
    }
    record = minic_c0_program_record(program, target->type.record_id);
    if (record == NULL || !record->is_complete ||
        !minic_riscv64_type_layout(program, target->type, &storage_size, &temporary_size) ||
        storage_size > SIZE_MAX - 15U) {
        return false;
    }
    if (storage_size == 0U) {
        return minic_c0_record_value_is_address_backed(program, source_id) &&
               minic_riscv64_emit_address_backed_record_value(
                   file, program, function, function_layout, source_id) &&
               minic_riscv64_emit_lvalue_address(
                   file, program, function, function_layout, target_id);
    }
    temporary_size = (storage_size + 15U) & ~(size_t)15U;

    return minic_riscv64_emit_record_value_temporary(
               file, program, function, function_layout, source_id, storage_size, temporary_size) &&
           minic_riscv64_emit_lvalue_address(file, program, function, function_layout, target_id) &&
           minic_riscv64_emit_record_temporary_to_address(
               file, storage_size, temporary_size, "a0", preserve_target_address);
}

static bool
minic_riscv64_emit_record_assignment_expression(FILE *file,
                                                const MinicC0Program *program,
                                                const MinicFunction *function,
                                                const MinicRiscv64FunctionLayout *function_layout,
                                                const MinicExpression *expression) {
    const MinicExpression *target;

    target = minic_c0_program_expression(program, expression->value.binary.left);
    return target != NULL && minic_type_equal(expression->type, target->type) &&
           minic_riscv64_emit_record_copy_value(file,
                                                program,
                                                function,
                                                function_layout,
                                                expression->value.binary.left,
                                                expression->value.binary.right,
                                                true);
}

static bool minic_riscv64_emit_builtin_unary(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicFunction *function,
                                             const MinicRiscv64FunctionLayout *function_layout,
                                             const MinicExpression *expression,
                                             MinicExpressionId expression_id) {
    const MinicExpression *operand;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.builtin_unary.operand);
    if (operand == NULL || !minic_type_equal(operand->type, minic_type_unsigned_long_long()) ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.builtin_unary.operand)) {
        return false;
    }

    /* __builtin_clzll(0) is undefined. For non-zero values this baseline RV64I
     * binary search computes the exact count without requiring Zbb clz. */
    return fprintf(file,
                   "  li t0, 0\n"
                   "  srli t1, a0, 32\n"
                   "  bnez t1, .Lminic_clzll_32_%zu\n"
                   "  addi t0, t0, 32\n"
                   "  slli a0, a0, 32\n"
                   ".Lminic_clzll_32_%zu:\n"
                   "  srli t1, a0, 48\n"
                   "  bnez t1, .Lminic_clzll_16_%zu\n"
                   "  addi t0, t0, 16\n"
                   "  slli a0, a0, 16\n"
                   ".Lminic_clzll_16_%zu:\n"
                   "  srli t1, a0, 56\n"
                   "  bnez t1, .Lminic_clzll_8_%zu\n"
                   "  addi t0, t0, 8\n"
                   "  slli a0, a0, 8\n"
                   ".Lminic_clzll_8_%zu:\n"
                   "  srli t1, a0, 60\n"
                   "  bnez t1, .Lminic_clzll_4_%zu\n"
                   "  addi t0, t0, 4\n"
                   "  slli a0, a0, 4\n"
                   ".Lminic_clzll_4_%zu:\n"
                   "  srli t1, a0, 62\n"
                   "  bnez t1, .Lminic_clzll_2_%zu\n"
                   "  addi t0, t0, 2\n"
                   "  slli a0, a0, 2\n"
                   ".Lminic_clzll_2_%zu:\n"
                   "  srli t1, a0, 63\n"
                   "  bnez t1, .Lminic_clzll_1_%zu\n"
                   "  addi t0, t0, 1\n"
                   ".Lminic_clzll_1_%zu:\n"
                   "  mv a0, t0\n",
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id) >= 0;
}

static bool minic_riscv64_emit_overflow_builtin(FILE *file,
                                                const MinicC0Program *program,
                                                const MinicFunction *function,
                                                const MinicRiscv64FunctionLayout *function_layout,
                                                const MinicExpression *expression) {
    const MinicExpression *left;
    const MinicExpression *right;
    const MinicExpression *result_pointer;
    MinicType result_type;
    size_t result_size;
    size_t result_alignment;
    bool is_unsigned;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_BUILTIN_OVERFLOW) {
        return false;
    }
    left = minic_c0_program_expression(program, expression->value.overflow.left);
    right = minic_c0_program_expression(program, expression->value.overflow.right);
    result_pointer =
        minic_c0_program_expression(program, expression->value.overflow.result_pointer);
    if (left == NULL || right == NULL || result_pointer == NULL ||
        !minic_type_pointee(result_pointer->type, &result_type) ||
        !minic_type_is_integer(result_type) || minic_type_is_bool_integer(result_type) ||
        !minic_type_equal(left->type, result_type) || !minic_type_equal(right->type, result_type) ||
        !minic_riscv64_type_layout(program, result_type, &result_size, &result_alignment) ||
        result_size == 0U || result_size > 8U) {
        return false;
    }
    (void)result_alignment;
    is_unsigned = minic_type_is_unsigned_integer(result_type);

    if (!minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.overflow.left) ||
        !minic_riscv64_emit_integer_conversion(file, result_type, "a0") ||
        !minic_riscv64_emit_stack_allocate(file, 16U) || fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.overflow.right) ||
        !minic_riscv64_emit_integer_conversion(file, result_type, "a0") ||
        !minic_riscv64_emit_stack_allocate(file, 16U) || fprintf(file, "  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.overflow.result_pointer) ||
        fprintf(file,
                "  mv t3, a0\n"
                "  ld t1, 0(sp)\n"
                "  ld t0, 16(sp)\n") < 0 ||
        !minic_riscv64_emit_stack_release(file, 32U)) {
        return false;
    }

    if (result_size < 8U) {
        const char *instruction;

        instruction = expression->value.overflow.operator_kind == MINIC_OVERFLOW_ADD        ? "add"
                      : expression->value.overflow.operator_kind == MINIC_OVERFLOW_SUBTRACT ? "sub"
                                                                                            : "mul";
        if (fprintf(file, "  %s t2, t0, t1\n  mv t4, t2\n", instruction) < 0 ||
            !minic_riscv64_emit_integer_conversion(file, result_type, "t2") ||
            fprintf(file, "  xor t4, t4, t2\n  snez a0, t4\n") < 0) {
            return false;
        }
    } else if (is_unsigned) {
        switch (expression->value.overflow.operator_kind) {
        case MINIC_OVERFLOW_ADD:
            if (fprintf(file, "  add t2, t0, t1\n  sltu a0, t2, t0\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_SUBTRACT:
            if (fprintf(file, "  sub t2, t0, t1\n  sltu a0, t0, t1\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_MULTIPLY:
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulhu t4, t0, t1\n"
                        "  snez a0, t4\n") < 0) {
                return false;
            }
            break;
        }
    } else {
        switch (expression->value.overflow.operator_kind) {
        case MINIC_OVERFLOW_ADD:
            if (fprintf(file,
                        "  add t2, t0, t1\n"
                        "  xor t4, t0, t2\n"
                        "  xor t5, t1, t2\n"
                        "  and t4, t4, t5\n"
                        "  slt a0, t4, zero\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_SUBTRACT:
            if (fprintf(file,
                        "  sub t2, t0, t1\n"
                        "  xor t4, t0, t1\n"
                        "  xor t5, t0, t2\n"
                        "  and t4, t4, t5\n"
                        "  slt a0, t4, zero\n") < 0) {
                return false;
            }
            break;
        case MINIC_OVERFLOW_MULTIPLY:
            if (fprintf(file,
                        "  mul t2, t0, t1\n"
                        "  mulh t4, t0, t1\n"
                        "  srai t5, t2, 63\n"
                        "  xor t4, t4, t5\n"
                        "  snez a0, t4\n") < 0) {
                return false;
            }
            break;
        }
    }
    return minic_riscv64_emit_scalar_store(file, result_type, "t2", "t3");
}

static bool minic_riscv64_emit_expression_impl(FILE *file,
                                               const MinicC0Program *program,
                                               const MinicFunction *function,
                                               const MinicRiscv64FunctionLayout *function_layout,
                                               MinicExpressionId expression_id,
                                               size_t record_result_temporary_size) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }

    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return fprintf(file, "  li a0, %" PRId64 "\n", expression->value.integer_value) >= 0 &&
               minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
    case MINIC_EXPRESSION_FLOATING:
        return minic_type_is_double(expression->type) &&
               fprintf(file, "  li a0, 0x%016" PRIx64 "\n", expression->value.floating_bits) >= 0;
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_load(
            file, program, function, function_layout, expression->value.local_id);
    case MINIC_EXPRESSION_COMPOUND_LITERAL:
        return minic_riscv64_emit_lvalue_address(
            file, program, function, function_layout, expression_id);
    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        if (object == NULL || object->name_length == 0U || minic_type_is_array(object->type) ||
            minic_type_is_record(object->type) ||
            fprintf(file, "  la a0, %s\n", object->name) < 0) {
            return false;
        }
        return minic_riscv64_emit_scalar_load(file, object->type, "a0", "a0");
    }
    case MINIC_EXPRESSION_FIXED_REGISTER: {
        const MinicFixedRegisterBinding *binding;

        binding = minic_c0_program_fixed_register_binding(
            program, expression->value.fixed_register_binding_id);
        if (binding == NULL || binding->register_name_length == 0U ||
            (!minic_type_is_integer(binding->type) && !minic_type_is_pointer(binding->type)) ||
            fprintf(file, "  mv a0, %s\n", binding->register_name) < 0) {
            return false;
        }
        return minic_type_is_pointer(binding->type) ||
               minic_riscv64_emit_integer_conversion(file, binding->type, "a0");
    }
    case MINIC_EXPRESSION_FUNCTION: {
        const MinicFunction *designator;
        MinicType function_type;

        designator = minic_c0_program_function(program, expression->value.function_id);
        return designator != NULL && designator->name_length != 0U &&
               minic_type_pointee(expression->type, &function_type) &&
               minic_type_is_function(function_type) &&
               fprintf(file, "  la a0, %s\n", minic_c0_function_symbol_name(designator)) >= 0;
    }
    case MINIC_EXPRESSION_BUILTIN_UNREACHABLE:
        /* Reaching this expression is undefined by GNU C. Keep the semantic
         * leaf for future CFG/IR reasoning, but do not invent a target trap or
         * external runtime symbol in the current non-optimizing backend. */
        return minic_type_is_void(expression->type);
    case MINIC_EXPRESSION_CALL_FRAME_ADDRESS: {
        MinicRiscv64FrameLayout frame_layout;
        MinicType pointee;

        if (function == NULL || expression->value.call_frame_address.level != 0U ||
            !minic_type_pointee(expression->type, &pointee) || !minic_type_is_void(pointee) ||
            !minic_riscv64_frame_layout_from_function_layout(
                program, function, function_layout, &frame_layout)) {
            return false;
        }
        switch (expression->value.call_frame_address.kind) {
        case MINIC_CALL_FRAME_ADDRESS_RETURN:
            return minic_riscv64_emit_s0_load64(file, "a0", frame_layout.saved_ra_offset);
        case MINIC_CALL_FRAME_ADDRESS_FRAME:
            return fprintf(file, "  mv a0, s0\n") >= 0;
        }
        return false;
    }
    case MINIC_EXPRESSION_LABEL_ADDRESS: {
        const MinicStatement *label;
        MinicType pointee;

        label = minic_c0_program_statement(program, expression->value.label_statement_id);
        return label != NULL && label->kind == MINIC_STATEMENT_LABEL &&
               minic_type_pointee(expression->type, &pointee) && minic_type_is_void(pointee) &&
               fprintf(file,
                       "  la a0, .Luser_%zu\n",
                       (size_t)expression->value.label_statement_id) >= 0;
    }
    case MINIC_EXPRESSION_SIZEOF: {
        MinicType measured_type;
        size_t size;

        measured_type = expression->value.sizeof_type;
        if (!minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            !minic_target_info_sizeof_type(
                minic_default_target_info(), program, measured_type, &size)) {
            return false;
        }
        return fprintf(file, "  li a0, %zu\n", size) >= 0;
    }
    case MINIC_EXPRESSION_OFFSETOF: {
        const MinicRecord *record;
        const MinicRecordField *field;

        size_t offset;

        record = minic_c0_program_record(program, expression->value.offsetof_value.record_id);
        field = minic_c0_record_field(record, expression->value.offsetof_value.field_index);
        if (record == NULL || field == NULL || !record->is_complete ||
            !minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   expression->value.offsetof_value.field_index,
                                                   &offset)) {
            return false;
        }
        return fprintf(file, "  li a0, %zu\n", offset) >= 0;
    }
    case MINIC_EXPRESSION_CAST:
        return false;
    case MINIC_EXPRESSION_BITCAST:
        if (!minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.unary.operand)) {
            return false;
        }
        if (minic_type_is_integer(expression->type)) {
            return minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
        }
        return minic_type_is_pointer(expression->type);
    case MINIC_EXPRESSION_DISCARD: {
        const MinicExpression *operand;
        MinicExpressionId operand_id;

        if (!minic_type_is_void(expression->type)) {
            return false;
        }
        operand_id = expression->value.unary.operand;
        operand = minic_c0_program_expression(program, operand_id);
        if (operand == NULL) {
            return false;
        }
        if (minic_type_is_record(operand->type) &&
            minic_c0_record_value_is_address_backed(program, operand_id)) {
            return minic_riscv64_emit_address_backed_record_value(
                file, program, function, function_layout, operand_id);
        }
        return minic_riscv64_emit_expression(file, program, function, function_layout, operand_id);
    }
    case MINIC_EXPRESSION_CONVERSION: {
        const MinicExpression *operand;
        const char *instruction;

        operand = minic_c0_program_expression(program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (minic_type_is_integer(expression->type) && minic_type_is_integer(operand->type)) {
            return minic_riscv64_emit_expression(
                       file, program, function, function_layout, expression->value.unary.operand) &&
                   minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
        }
        if (minic_type_is_double(expression->type) && minic_type_is_float(operand->type)) {
            return minic_riscv64_emit_expression(
                       file, program, function, function_layout, expression->value.unary.operand) &&
                   fprintf(file,
                           "  fmv.w.x ft0, a0\n"
                           "  fcvt.d.s ft0, ft0\n"
                           "  fmv.x.d a0, ft0\n") >= 0;
        }
        if (minic_type_is_double(expression->type) && minic_type_is_integer(operand->type)) {
            return minic_riscv64_emit_expression(
                       file, program, function, function_layout, expression->value.unary.operand) &&
                   minic_riscv64_emit_integer_to_double(file, operand->type, "a0", "ft0") &&
                   fprintf(file, "  fmv.x.d a0, ft0\n") >= 0;
        }
        if (!minic_type_is_integer(expression->type) || !minic_type_is_double(operand->type)) {
            return false;
        }
        if (minic_type_is_long_integer(expression->type)) {
            instruction =
                minic_type_is_unsigned_integer(expression->type) ? "fcvt.lu.d" : "fcvt.l.d";
        } else {
            instruction =
                minic_type_is_unsigned_integer(expression->type) ? "fcvt.wu.d" : "fcvt.w.d";
        }
        return minic_riscv64_emit_expression(
                   file, program, function, function_layout, expression->value.unary.operand) &&
               fprintf(file,
                       "  fmv.d.x ft0, a0\n"
                       "  %s a0, ft0, rtz\n",
                       instruction) >= 0 &&
               minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
    }
    case MINIC_EXPRESSION_ADDRESS_OF: {
        const MinicExpression *operand;

        operand = minic_c0_program_expression(program, expression->value.unary.operand);
        if (operand == NULL) {
            return false;
        }
        if (operand->kind == MINIC_EXPRESSION_FUNCTION || minic_type_is_function(operand->type)) {
            return minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.unary.operand);
        }
        return minic_riscv64_emit_lvalue_address(
            file, program, function, function_layout, expression->value.unary.operand);
    }
    case MINIC_EXPRESSION_DEREFERENCE:
        if (minic_type_is_function(expression->type)) {
            return minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.unary.operand);
        }
        return minic_riscv64_emit_expression(
                   file, program, function, function_layout, expression->value.unary.operand) &&
               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_SUBSCRIPT:
        return minic_riscv64_emit_subscript_address(
                   file, program, function, function_layout, expression) &&
               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field == NULL) {
            return false;
        }
        if (expression->value_category == MINIC_VALUE_RVALUE) {
            return minic_riscv64_emit_record_rvalue_member(
                file, program, function, function_layout, expression, expression_id);
        }
        if (!minic_riscv64_emit_member_address(
                file, program, function, function_layout, expression)) {
            return false;
        }
        if (field->is_array) {
            return expression->value_category == MINIC_VALUE_LVALUE;
        }
        return minic_riscv64_emit_lvalue_load_from_address(
            file, program, expression_id, expression->type, "a0", "a0");
    }
    case MINIC_EXPRESSION_LVALUE_READ:
        return minic_riscv64_emit_lvalue_address(
                   file, program, function, function_layout, expression->value.unary.operand) &&
               minic_riscv64_emit_lvalue_load_from_address(
                   file, program, expression->value.unary.operand, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        if (target != NULL && minic_type_is_record(target->type)) {
            return minic_riscv64_emit_record_assignment_expression(
                file, program, function, function_layout, expression);
        }
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            !minic_c0_assignment_compatible(
                program, target->type, expression->value.binary.right) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, function_layout, expression->value.binary.left) ||
            fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.binary.right)) {
            return false;
        }
        if (minic_type_is_integer(target->type) &&
            !minic_riscv64_emit_integer_conversion(file, target->type, "a0")) {
            return false;
        }
        return fprintf(file,
                       "  mv t0, a0\n"
                       "  ld t1, 0(sp)\n"
                       "  addi sp, sp, 16\n") >= 0 &&
               minic_riscv64_emit_lvalue_store_to_address(
                   file, program, expression->value.binary.left, target->type, "t0", "t1") &&
               fprintf(file, "  mv a0, t0\n") >= 0;
    }
    case MINIC_EXPRESSION_COMPOUND_ASSIGNMENT: {
        const MinicExpression *target;
        const MinicExpression *value;
        MinicBinaryOperator operator_kind;

        target = minic_c0_program_expression(program, expression->value.binary.left);
        value = minic_c0_program_expression(program, expression->value.binary.right);
        operator_kind = expression->value.binary.operator_kind;
        if (target == NULL || value == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            !minic_riscv64_emit_lvalue_address(
                file, program, function, function_layout, expression->value.binary.left) ||
            fprintf(file, "  addi sp, sp, -32\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_lvalue_load_from_address(
                file, program, expression->value.binary.left, target->type, "a0", "a0")) {
            return false;
        }
        if (minic_type_is_pointer(target->type)) {
            size_t element_size;

            if ((operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT) ||
                !minic_type_is_integer(value->type) ||
                !minic_riscv64_pointer_element_size(program, target->type, &element_size) ||
                fprintf(file, "  sd a0, 8(sp)\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, function_layout, expression->value.binary.right) ||
                !minic_riscv64_emit_scale_register(file, "a0", "t0", element_size) ||
                fprintf(file,
                        "  ld t0, 8(sp)\n"
                        "  %s a0, t0, a0\n",
                        operator_kind == MINIC_BINARY_ADD ? "add" : "sub") < 0) {
                return false;
            }
        } else if (minic_type_is_double(target->type)) {
            if ((operator_kind != MINIC_BINARY_ADD && operator_kind != MINIC_BINARY_SUBTRACT &&
                 operator_kind != MINIC_BINARY_MULTIPLY && operator_kind != MINIC_BINARY_DIVIDE) ||
                (!minic_type_is_double(value->type) && !minic_type_is_integer(value->type)) ||
                fprintf(file, "  sd a0, 8(sp)\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, function_layout, expression->value.binary.right) ||
                fprintf(file, "  ld t0, 8(sp)\n") < 0 ||
                !minic_riscv64_emit_double_binary(file, operator_kind, target->type, value->type)) {
                return false;
            }
        } else {
            MinicType common_type;
            const char *opcode;

            if (!minic_type_is_integer(target->type) || !minic_type_is_integer(value->type) ||
                !minic_target_info_integer_common(
                    minic_default_target_info(), target->type, value->type, &common_type) ||
                !minic_riscv64_emit_normalize_integer(file, common_type, "a0") ||
                fprintf(file, "  sd a0, 8(sp)\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file, program, function, function_layout, expression->value.binary.right) ||
                !minic_riscv64_emit_normalize_integer(file, common_type, "a0")) {
                return false;
            }
            switch (operator_kind) {
            case MINIC_BINARY_ADD:
                opcode = minic_type_is_long_integer(common_type) ? "add" : "addw";
                break;
            case MINIC_BINARY_SUBTRACT:
                opcode = minic_type_is_long_integer(common_type) ? "sub" : "subw";
                break;
            case MINIC_BINARY_MULTIPLY:
                opcode = minic_type_is_long_integer(common_type) ? "mul" : "mulw";
                break;
            case MINIC_BINARY_DIVIDE:
                if (minic_type_is_unsigned_integer(common_type)) {
                    opcode = minic_type_is_long_integer(common_type) ? "divu" : "divuw";
                } else {
                    opcode = minic_type_is_long_integer(common_type) ? "div" : "divw";
                }
                break;
            case MINIC_BINARY_REMAINDER:
                if (minic_type_is_unsigned_integer(common_type)) {
                    opcode = minic_type_is_long_integer(common_type) ? "remu" : "remuw";
                } else {
                    opcode = minic_type_is_long_integer(common_type) ? "rem" : "remw";
                }
                break;
            case MINIC_BINARY_BITWISE_AND:
                opcode = "and";
                break;
            case MINIC_BINARY_BITWISE_OR:
                opcode = "or";
                break;
            case MINIC_BINARY_BITWISE_XOR:
                opcode = "xor";
                break;
            case MINIC_BINARY_SHIFT_LEFT:
                opcode = minic_type_is_long_integer(common_type) ? "sll" : "sllw";
                break;
            case MINIC_BINARY_SHIFT_RIGHT:
                if (minic_type_is_unsigned_integer(common_type)) {
                    opcode = minic_type_is_long_integer(common_type) ? "srl" : "srlw";
                } else {
                    opcode = minic_type_is_long_integer(common_type) ? "sra" : "sraw";
                }
                break;
            default:
                return false;
            }
            if (fprintf(file,
                        "  ld t0, 8(sp)\n"
                        "  %s a0, t0, a0\n",
                        opcode) < 0 ||
                !minic_riscv64_emit_integer_conversion(file, target->type, "a0")) {
                return false;
            }
        }
        return fprintf(file,
                       "  mv t0, a0\n"
                       "  ld t1, 0(sp)\n"
                       "  addi sp, sp, 32\n") >= 0 &&
               minic_riscv64_emit_lvalue_store_to_address(
                   file, program, expression->value.binary.left, target->type, "t0", "t1") &&
               fprintf(file, "  mv a0, t0\n") >= 0;
    }
    case MINIC_EXPRESSION_UNARY:
        if (expression->value.unary.operator_kind == MINIC_UNARY_POST_INCREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_POST_DECREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_PRE_INCREMENT ||
            expression->value.unary.operator_kind == MINIC_UNARY_PRE_DECREMENT) {
            return minic_riscv64_emit_update(file, program, function, function_layout, expression);
        }
        if (!minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.unary.operand)) {
            return false;
        }
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            if (minic_type_is_double(expression->type)) {
                return true;
            }
            return minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_UNARY_NEGATE:
            if (minic_type_is_double(expression->type)) {
                return fprintf(file,
                               "  fmv.d.x ft0, a0\n"
                               "  fneg.d ft0, ft0\n"
                               "  fmv.x.d a0, ft0\n") >= 0;
            }
            return fprintf(file,
                           minic_type_is_long_integer(expression->type) ? "  neg a0, a0\n"
                                                                        : "  negw a0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_UNARY_LOGICAL_NOT:
            return fprintf(file, "  seqz a0, a0\n") >= 0;
        case MINIC_UNARY_BITWISE_NOT:
            return fprintf(file, "  not a0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        }
        return false;
    case MINIC_EXPRESSION_BINARY: {
        const MinicExpression *left;
        const MinicExpression *right;
        MinicType common_integer_type;
        bool has_integer_common_type;
        bool has_pointer_equality;
        bool has_pointer_relational;
        size_t element_size;

        if (expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
            return minic_riscv64_emit_expression(
                       file, program, function, function_layout, expression->value.binary.left) &&
                   minic_riscv64_emit_expression(
                       file, program, function, function_layout, expression->value.binary.right);
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
            expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
            return minic_riscv64_emit_logical_binary(
                file, program, function, function_layout, expression, expression_id);
        }

        left = minic_c0_program_expression(program, expression->value.binary.left);
        right = minic_c0_program_expression(program, expression->value.binary.right);
        has_integer_common_type =
            left != NULL && right != NULL &&
            minic_target_info_integer_common(
                minic_default_target_info(), left->type, right->type, &common_integer_type);
        has_pointer_equality = minic_c0_pointer_equality_compatible(
            program, expression->value.binary.left, expression->value.binary.right);
        has_pointer_relational = left != NULL && right != NULL &&
                                 minic_type_is_pointer(left->type) &&
                                 minic_type_is_pointer(right->type) &&
                                 minic_type_equal(expression->type, minic_type_int());
        if (left == NULL || right == NULL ||
            !minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.binary.left) ||
            (has_integer_common_type &&
             !minic_riscv64_emit_normalize_integer(file, common_integer_type, "a0")) ||
            fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, function_layout, expression->value.binary.right) ||
            (has_integer_common_type &&
             !minic_riscv64_emit_normalize_integer(file, common_integer_type, "a0")) ||
            fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") < 0) {
            return false;
        }
        if (minic_type_equal(expression->type, minic_type_int()) &&
            (minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
            (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
            (minic_type_is_double(left->type) || minic_type_is_double(right->type))) {
            return minic_riscv64_emit_double_comparison(
                file, expression->value.binary.operator_kind, left->type, right->type);
        }
        if (minic_type_is_double(expression->type) &&
            (minic_type_is_double(left->type) || minic_type_is_integer(left->type)) &&
            (minic_type_is_double(right->type) || minic_type_is_integer(right->type)) &&
            (minic_type_is_double(left->type) || minic_type_is_double(right->type))) {
            return minic_riscv64_emit_double_binary(
                file, expression->value.binary.operator_kind, left->type, right->type);
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            if (has_integer_common_type) {
                return fprintf(file,
                               minic_type_is_long_integer(common_integer_type)
                                   ? "  add a0, t0, a0\n"
                                   : "  addw a0, t0, a0\n") >= 0 &&
                       minic_riscv64_emit_integer_result_conversion(
                           file, common_integer_type, expression->type, "a0");
            }
            if (minic_type_is_pointer(left->type) && minic_type_is_integer(right->type) &&
                minic_riscv64_pointer_element_size(program, left->type, &element_size)) {
                return minic_riscv64_emit_scale_register(file, "a0", "t1", element_size) &&
                       fprintf(file, "  add a0, t0, a0\n") >= 0;
            }
            if (minic_type_is_integer(left->type) && minic_type_is_pointer(right->type) &&
                minic_riscv64_pointer_element_size(program, right->type, &element_size)) {
                return minic_riscv64_emit_scale_register(file, "t0", "t1", element_size) &&
                       fprintf(file, "  add a0, a0, t0\n") >= 0;
            }
            return false;
        case MINIC_BINARY_SUBTRACT:
            if (has_integer_common_type) {
                return fprintf(file,
                               minic_type_is_long_integer(common_integer_type)
                                   ? "  sub a0, t0, a0\n"
                                   : "  subw a0, t0, a0\n") >= 0 &&
                       minic_riscv64_emit_integer_result_conversion(
                           file, common_integer_type, expression->type, "a0");
            }
            if (minic_type_is_pointer(left->type) && minic_type_is_pointer(right->type) &&
                minic_type_equal(expression->type, minic_type_long()) &&
                minic_riscv64_pointer_element_size(program, left->type, &element_size)) {
                if (element_size == 1U) {
                    return fprintf(file, "  sub a0, t0, a0\n") >= 0;
                }
                return fprintf(file,
                               "  sub a0, t0, a0\n"
                               "  li t1, %zu\n"
                               "  div a0, a0, t1\n",
                               element_size) >= 0;
            }
            if (minic_type_is_pointer(left->type) && minic_type_is_integer(right->type) &&
                minic_riscv64_pointer_element_size(program, left->type, &element_size)) {
                return minic_riscv64_emit_scale_register(file, "a0", "t1", element_size) &&
                       fprintf(file, "  sub a0, t0, a0\n") >= 0;
            }
            return false;
        case MINIC_BINARY_MULTIPLY:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(common_integer_type)
                               ? "  mul a0, t0, a0\n"
                               : "  mulw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_DIVIDE:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(common_integer_type)
                               ? (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  divu a0, t0, a0\n"
                                      : "  div a0, t0, a0\n")
                               : (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  divuw a0, t0, a0\n"
                                      : "  divw a0, t0, a0\n")) >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_REMAINDER:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(common_integer_type)
                               ? (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  remu a0, t0, a0\n"
                                      : "  rem a0, t0, a0\n")
                               : (minic_type_is_unsigned_integer(common_integer_type)
                                      ? "  remuw a0, t0, a0\n"
                                      : "  remw a0, t0, a0\n")) >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_SHIFT_LEFT:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(expression->type)
                               ? "  sll a0, t0, a0\n"
                               : "  sllw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_BINARY_SHIFT_RIGHT:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_long_integer(expression->type)
                               ? (minic_type_is_unsigned_integer(expression->type)
                                      ? "  srl a0, t0, a0\n"
                                      : "  sra a0, t0, a0\n")
                               : (minic_type_is_unsigned_integer(expression->type)
                                      ? "  srlw a0, t0, a0\n"
                                      : "  sraw a0, t0, a0\n")) >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_BINARY_BITWISE_AND:
            return has_integer_common_type && fprintf(file, "  and a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_BITWISE_XOR:
            return has_integer_common_type && fprintf(file, "  xor a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_BITWISE_OR:
            return has_integer_common_type && fprintf(file, "  or a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_integer_result_conversion(
                       file, common_integer_type, expression->type, "a0");
        case MINIC_BINARY_EQUAL:
            return (has_integer_common_type || has_pointer_equality) &&
                   fprintf(file, "  xor a0, t0, a0\n  seqz a0, a0\n") >= 0;
        case MINIC_BINARY_NOT_EQUAL:
            return (has_integer_common_type || has_pointer_equality) &&
                   fprintf(file, "  xor a0, t0, a0\n  snez a0, a0\n") >= 0;
        case MINIC_BINARY_LESS:
            if (has_pointer_relational) {
                return fprintf(file, "  sltu a0, t0, a0\n") >= 0;
            }
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, t0, a0\n"
                               : "  slt a0, t0, a0\n") >= 0;
        case MINIC_BINARY_LESS_EQUAL:
            if (has_pointer_relational) {
                return fprintf(file, "  sltu a0, a0, t0\n  xori a0, a0, 1\n") >= 0;
            }
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, a0, t0\n  xori a0, a0, 1\n"
                               : "  slt a0, a0, t0\n  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_GREATER:
            if (has_pointer_relational) {
                return fprintf(file, "  sltu a0, a0, t0\n") >= 0;
            }
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, a0, t0\n"
                               : "  slt a0, a0, t0\n") >= 0;
        case MINIC_BINARY_GREATER_EQUAL:
            if (has_pointer_relational) {
                return fprintf(file, "  sltu a0, t0, a0\n  xori a0, a0, 1\n") >= 0;
            }
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, t0, a0\n  xori a0, a0, 1\n"
                               : "  slt a0, t0, a0\n  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
        case MINIC_BINARY_COMMA:
            return false;
        }
        return false;
    }
    case MINIC_EXPRESSION_CONDITIONAL: {
        const MinicExpression *condition;
        const MinicExpression *when_true;
        const MinicExpression *when_false;

        condition = minic_c0_program_expression(program, expression->value.conditional.condition);
        when_true = minic_c0_program_expression(program, expression->value.conditional.when_true);
        when_false = minic_c0_program_expression(program, expression->value.conditional.when_false);
        if (condition == NULL || when_true == NULL || when_false == NULL ||
            !type_is_condition_scalar(condition->type) ||
            (expression->value.conditional.uses_condition_value &&
             expression->value.conditional.when_true != expression->value.conditional.condition) ||
            !minic_riscv64_emit_expression(file,
                                           program,
                                           function,
                                           function_layout,
                                           expression->value.conditional.condition) ||
            fprintf(file, "  beqz a0, .Lminic_cond_false_%zu\n", expression_id) < 0) {
            return false;
        }
        if (expression->value.conditional.uses_condition_value) {
            if (!minic_riscv64_emit_conditional_result_conversion(
                    file, condition->type, expression->type)) {
                return false;
            }
        } else if (!minic_riscv64_emit_expression(file,
                                                  program,
                                                  function,
                                                  function_layout,
                                                  expression->value.conditional.when_true) ||
                   !minic_riscv64_emit_conditional_result_conversion(
                       file, when_true->type, expression->type)) {
            return false;
        }
        if (fprintf(file,
                    "  j .Lminic_cond_end_%zu\n"
                    ".Lminic_cond_false_%zu:\n",
                    expression_id,
                    expression_id) < 0 ||
            !minic_riscv64_emit_expression(file,
                                           program,
                                           function,
                                           function_layout,
                                           expression->value.conditional.when_false) ||
            !minic_riscv64_emit_conditional_result_conversion(
                file, when_false->type, expression->type)) {
            return false;
        }
        return fprintf(file, ".Lminic_cond_end_%zu:\n", expression_id) >= 0;
    }
    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return minic_riscv64_emit_builtin_unary(
            file, program, function, function_layout, expression, expression_id);
    case MINIC_EXPRESSION_BUILTIN_OVERFLOW:
        return minic_riscv64_emit_overflow_builtin(
            file, program, function, function_layout, expression);
    case MINIC_EXPRESSION_STATEMENT: {
        if (!minic_riscv64_emit_expression_owned_block(
                file,
                program,
                function,
                function_layout,
                expression_id,
                expression->value.statement_expression.block)) {
            return false;
        }
        if (expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID) {
            return minic_type_is_void(expression->type);
        }
        if (minic_type_is_record(expression->type)) {
            const MinicExpression *result;

            result =
                minic_c0_program_expression(program, expression->value.statement_expression.result);
            if (result == NULL || !minic_type_is_record(result->type) ||
                result->type.record_id != expression->type.record_id) {
                return false;
            }
            return minic_riscv64_emit_address_backed_record_value(
                file,
                program,
                function,
                function_layout,
                expression->value.statement_expression.result);
        }
        return minic_riscv64_emit_expression(file,
                                             program,
                                             function,
                                             function_layout,
                                             expression->value.statement_expression.result);
    }
    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *direct_callee;
        const MinicFunctionType *indirect_type;
        const MinicExpression *indirect_callee;
        const MinicType *parameter_types;
        MinicType abi_parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiValue abi_values[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiArgumentLocation argument_locations[MINIC_MAX_FUNCTION_PARAMETERS];
        MinicRiscv64AbiCursor initial_abi_cursor;
        MinicRiscv64AbiValue return_value;
        bool has_indirect_return;
        bool use_formal_location_path;
        size_t argument_stage_end[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t parameter_count;
        size_t argument_count;
        size_t argument_index;
        size_t outgoing_stack_bytes;
        size_t stack_argument_count;
        size_t staged_bytes;
        size_t temporary_bytes;
        bool is_indirect;
        bool is_variadic;

        direct_callee = NULL;
        indirect_type = NULL;
        indirect_callee = NULL;
        parameter_types = NULL;
        parameter_count = 0U;
        is_variadic = false;
        is_indirect = expression->value.call.function_id == MINIC_FUNCTION_INVALID;
        argument_count = expression->value.call.argument_count;
        outgoing_stack_bytes = 0U;
        stack_argument_count = 0U;
        staged_bytes = 0U;
        (void)memset(argument_stage_end, 0, sizeof(argument_stage_end));
        if (!minic_riscv64_abi_cursor_initialize_for_return(
                program, expression->type, &initial_abi_cursor, &return_value)) {
            return false;
        }
        has_indirect_return = return_value.kind == MINIC_RISCV64_ABI_VALUE_INDIRECT;
        if (has_indirect_return) {
            if (!minic_type_is_record(expression->type) || return_value.storage_size <= 16U ||
                record_result_temporary_size < return_value.storage_size ||
                (record_result_temporary_size & 15U) != 0U ||
                !minic_riscv64_emit_stack_allocate(file, record_result_temporary_size)) {
                return false;
            }
        } else if (record_result_temporary_size != 0U) {
            return false;
        }

        if (is_indirect) {
            MinicType function_type;

            indirect_callee = minic_c0_program_expression(program, expression->value.call.callee);
            if (indirect_callee == NULL) {
                return false;
            }
            function_type = indirect_callee->type;
            if (!minic_type_is_function(function_type) &&
                (!minic_type_pointee(indirect_callee->type, &function_type) ||
                 !minic_type_is_function(function_type))) {
                return false;
            }
            indirect_type = minic_c0_program_function_type(program, function_type.function_type_id);
            if (indirect_type == NULL ||
                indirect_type->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                argument_count != indirect_type->parameter_count ||
                !minic_type_equal(expression->type, indirect_type->return_type) ||
                !minic_riscv64_emit_expression(
                    file, program, function, function_layout, expression->value.call.callee) ||
                fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0) {
                return false;
            }
            staged_bytes = 16U;
            parameter_types = indirect_type->parameter_types;
            parameter_count = indirect_type->parameter_count;
        } else {
            direct_callee = minic_c0_program_function(program, expression->value.call.function_id);
            if (direct_callee == NULL || direct_callee->name_length == 0U ||
                direct_callee->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
                return false;
            }
            parameter_types = direct_callee->parameter_types;
            parameter_count = direct_callee->parameter_count;
            is_variadic = direct_callee->is_variadic;
            if (argument_count < parameter_count ||
                argument_count > MINIC_MAX_FUNCTION_PARAMETERS ||
                (!is_variadic && argument_count != parameter_count)) {
                return false;
            }
        }

        for (argument_index = 0U; argument_index < parameter_count; ++argument_index) {
            if (!minic_c0_fixed_parameter_abi_type(program,
                                                   parameter_types[argument_index],
                                                   &abi_parameter_types[argument_index]) ||
                !minic_riscv64_classify_abi_value(
                    program, abi_parameter_types[argument_index], &abi_values[argument_index])) {
                return false;
            }
        }

        if (!is_indirect && direct_callee != NULL && direct_callee->name_length == 16U &&
            strcmp(direct_callee->name, "__minic_va_start") == 0) {
            MinicRiscv64FrameLayout frame_layout;

            if (argument_count != 0U || !function->is_variadic ||
                !minic_type_is_pointer(expression->type) ||
                !minic_riscv64_frame_layout_from_function_layout(
                    program, function, function_layout, &frame_layout)) {
                return false;
            }
            if (frame_layout.varargs_offset <= 2047U) {
                return fprintf(file, "  addi a0, s0, %zu\n", frame_layout.varargs_offset) >= 0;
            }
            return fprintf(file,
                           "  li t0, %zu\n"
                           "  add a0, s0, t0\n",
                           frame_layout.varargs_offset) >= 0;
        }

        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicExpression *argument;

            argument = minic_c0_program_expression(
                program, expression->value.call.arguments[argument_index]);
            if (argument == NULL) {
                return false;
            }
            if (argument_index < parameter_count &&
                minic_type_is_record(abi_parameter_types[argument_index])) {
                const MinicRiscv64AbiValue *abi_value;
                size_t stage_size;

                abi_value = &abi_values[argument_index];
                if (!minic_type_is_record(argument->type) ||
                    argument->type.record_id != abi_parameter_types[argument_index].record_id) {
                    return false;
                }
                if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                    argument_stage_end[argument_index] = staged_bytes;
                    continue;
                }
                if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                    stage_size = 16U;
                } else if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                    if (abi_value->storage_size > SIZE_MAX - 15U) {
                        return false;
                    }
                    stage_size = (abi_value->storage_size + 15U) & ~(size_t)15U;
                } else {
                    return false;
                }
                if (stage_size == 0U || staged_bytes > SIZE_MAX - stage_size ||
                    !minic_c0_record_value_is_copy_source(
                        program, expression->value.call.arguments[argument_index]) ||
                    !minic_riscv64_emit_record_value_temporary(
                        file,
                        program,
                        function,
                        function_layout,
                        expression->value.call.arguments[argument_index],
                        abi_value->storage_size,
                        stage_size)) {
                    return false;
                }
                staged_bytes += stage_size;
                argument_stage_end[argument_index] = staged_bytes;
                continue;
            }
            if (!minic_riscv64_emit_expression(file,
                                               program,
                                               function,
                                               function_layout,
                                               expression->value.call.arguments[argument_index])) {
                return false;
            }
            if (argument_index < parameter_count) {
                if (minic_type_is_integer(abi_parameter_types[argument_index]) &&
                    !minic_riscv64_emit_integer_conversion(
                        file, abi_parameter_types[argument_index], "a0")) {
                    return false;
                }
            } else if (!is_variadic ||
                       !minic_riscv64_emit_variadic_argument_conversion(file, argument->type)) {
                return false;
            }
            if (staged_bytes > SIZE_MAX - 16U || !minic_riscv64_emit_stack_allocate(file, 16U) ||
                fprintf(file, "  sd a0, 0(sp)\n") < 0) {
                return false;
            }
            staged_bytes += 16U;
            argument_stage_end[argument_index] = staged_bytes;
        }
        use_formal_location_path = true;
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicRiscv64AbiValue *value;

            value = &abi_values[argument_index];
            if (value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE ||
                value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT ||
                (value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE &&
                 (argument_index >= parameter_count ||
                  (value->storage_size != 8U && value->storage_size != 16U)))) {
                use_formal_location_path = false;
                break;
            }
        }
        if (use_formal_location_path) {
            MinicRiscv64AbiCursor abi_cursor;

            abi_cursor = initial_abi_cursor;
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                const MinicExpression *argument;
                MinicType placement_type;
                bool is_fixed_parameter;

                argument = minic_c0_program_expression(
                    program, expression->value.call.arguments[argument_index]);
                if (argument == NULL) {
                    return false;
                }
                is_fixed_parameter = argument_index < parameter_count;
                placement_type =
                    is_fixed_parameter ? abi_parameter_types[argument_index] : argument->type;
                if (!minic_riscv64_abi_place_argument(program,
                                                      placement_type,
                                                      is_fixed_parameter,
                                                      &abi_cursor,
                                                      &argument_locations[argument_index])) {
                    return false;
                }
            }
            stack_argument_count = abi_cursor.stack_slot_count;
        } else {
            {
                size_t integer_register_index;
                size_t floating_register_index;

                integer_register_index = has_indirect_return ? 1U : 0U;
                floating_register_index = 0U;
                for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                    bool fixed_floating;

                    fixed_floating = argument_index < parameter_count &&
                                     (minic_type_is_double(abi_parameter_types[argument_index]) ||
                                      minic_type_is_float(abi_parameter_types[argument_index]));
                    if (fixed_floating) {
                        if (floating_register_index >= 8U) {
                            return false;
                        }
                        floating_register_index += 1U;
                    } else if (argument_index < parameter_count &&
                               minic_type_is_record(abi_parameter_types[argument_index])) {
                        const MinicRiscv64AbiValue *abi_value;
                        size_t integer_slots;
                        size_t slot_index;

                        abi_value = &abi_values[argument_index];
                        if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                            continue;
                        }
                        if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                            integer_slots = abi_value->register_chunks;
                        } else if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                            integer_slots = 1U;
                        } else {
                            return false;
                        }
                        for (slot_index = 0U; slot_index < integer_slots; ++slot_index) {
                            if (integer_register_index < 8U) {
                                integer_register_index += 1U;
                            } else {
                                stack_argument_count += 1U;
                            }
                        }
                    } else if (integer_register_index < 8U) {
                        integer_register_index += 1U;
                    } else {
                        stack_argument_count += 1U;
                    }
                }
            }
        }
        if (stack_argument_count > (SIZE_MAX - 15U) / 8U) {
            return false;
        }
        outgoing_stack_bytes = (stack_argument_count * 8U + 15U) & ~(size_t)15U;
        if (outgoing_stack_bytes != 0U &&
            !minic_riscv64_emit_stack_allocate(file, outgoing_stack_bytes)) {
            return false;
        }
        if (use_formal_location_path) {
            for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                const MinicRiscv64AbiArgumentLocation *location;
                size_t offset;
                bool is_fixed_parameter;

                location = &argument_locations[argument_index];
                is_fixed_parameter = argument_index < parameter_count;
                if (argument_stage_end[argument_index] > staged_bytes) {
                    return false;
                }
                offset = outgoing_stack_bytes + staged_bytes - argument_stage_end[argument_index];

                if (location->floating_register_count == 1U) {
                    if (!is_fixed_parameter ||
                        location->value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT ||
                        location->floating_register_begin >= 8U ||
                        location->integer_register_count != 0U ||
                        location->stack_slot_count != 0U ||
                        fprintf(file,
                                minic_type_is_double(abi_parameter_types[argument_index])
                                    ? "  ld t0, %zu(sp)\n  fmv.d.x fa%zu, t0\n"
                                    : "  ld t0, %zu(sp)\n  fmv.w.x fa%zu, t0\n",
                                offset,
                                location->floating_register_begin) < 0) {
                        return false;
                    }
                    continue;
                }

                if (location->value.kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                    size_t chunk_index;

                    if (location->value.storage_size != 8U && location->value.storage_size != 16U) {
                        return false;
                    }
                    if (location->value.slot_count == 0U ||
                        location->value.slot_count !=
                            location->integer_register_count + location->stack_slot_count ||
                        location->integer_register_begin > 8U ||
                        location->integer_register_count > 8U - location->integer_register_begin) {
                        return false;
                    }
                    for (chunk_index = 0U; chunk_index < location->value.slot_count;
                         ++chunk_index) {
                        size_t chunk_offset;

                        chunk_offset = offset + chunk_index * 8U;
                        if (chunk_index < location->integer_register_count) {
                            size_t register_index;

                            register_index = location->integer_register_begin + chunk_index;
                            if (fprintf(
                                    file, "  ld a%zu, %zu(sp)\n", register_index, chunk_offset) <
                                0) {
                                return false;
                            }
                        } else {
                            size_t stack_slot;

                            stack_slot = location->stack_slot_begin +
                                         (chunk_index - location->integer_register_count);
                            if (!minic_riscv64_emit_sp_load64(file, "t0", chunk_offset) ||
                                !minic_riscv64_emit_sp_store64(file, "t0", stack_slot * 8U)) {
                                return false;
                            }
                        }
                    }
                    continue;
                }

                if (location->floating_register_count != 0U ||
                    (location->value.kind != MINIC_RISCV64_ABI_VALUE_INTEGER &&
                     location->value.kind != MINIC_RISCV64_ABI_VALUE_FLOAT)) {
                    return false;
                }
                if (location->integer_register_count == 1U && location->stack_slot_count == 0U &&
                    location->integer_register_begin < 8U) {
                    if (fprintf(file,
                                "  ld a%zu, %zu(sp)\n",
                                location->integer_register_begin,
                                offset) < 0) {
                        return false;
                    }
                    continue;
                }
                if (location->integer_register_count == 0U && location->stack_slot_count == 1U) {
                    if (!minic_riscv64_emit_sp_load64(file, "t0", offset) ||
                        !minic_riscv64_emit_sp_store64(
                            file, "t0", location->stack_slot_begin * 8U)) {
                        return false;
                    }
                    continue;
                }
                return false;
            }
        } else {
            {
                size_t integer_register_index;
                size_t floating_register_index;
                size_t stack_argument_index;

                integer_register_index = has_indirect_return ? 1U : 0U;
                floating_register_index = 0U;
                stack_argument_index = 0U;
                for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
                    size_t offset;
                    bool fixed_floating;

                    if (argument_stage_end[argument_index] > staged_bytes) {
                        return false;
                    }
                    offset =
                        outgoing_stack_bytes + staged_bytes - argument_stage_end[argument_index];
                    fixed_floating = argument_index < parameter_count &&
                                     (minic_type_is_double(abi_parameter_types[argument_index]) ||
                                      minic_type_is_float(abi_parameter_types[argument_index]));
                    if (fixed_floating) {
                        if (floating_register_index >= 8U ||
                            fprintf(file,
                                    minic_type_is_double(abi_parameter_types[argument_index])
                                        ? "  ld t0, %zu(sp)\n  fmv.d.x fa%zu, t0\n"
                                        : "  ld t0, %zu(sp)\n  fmv.w.x fa%zu, t0\n",
                                    offset,
                                    floating_register_index) < 0) {
                            return false;
                        }
                        floating_register_index += 1U;
                    } else if (argument_index < parameter_count &&
                               minic_type_is_record(abi_parameter_types[argument_index])) {
                        const MinicRiscv64AbiValue *abi_value;

                        abi_value = &abi_values[argument_index];
                        if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_IGNORE) {
                            continue;
                        }
                        if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_INDIRECT) {
                            const char *destination_register;

                            destination_register =
                                integer_register_index < 8U
                                    ? minic_riscv64_argument_registers[integer_register_index]
                                    : "t0";
                            if (offset <= 2047U) {
                                if (fprintf(file,
                                            "  addi %s, sp, %zu\n",
                                            destination_register,
                                            offset) < 0) {
                                    return false;
                                }
                            } else if (fprintf(file,
                                               "  li t1, %zu\n  add %s, sp, t1\n",
                                               offset,
                                               destination_register) < 0) {
                                return false;
                            }
                            if (integer_register_index < 8U) {
                                integer_register_index += 1U;
                            } else {
                                if (!minic_riscv64_emit_sp_store64(
                                        file, "t0", stack_argument_index * 8U)) {
                                    return false;
                                }
                                stack_argument_index += 1U;
                            }
                        } else if (abi_value->kind == MINIC_RISCV64_ABI_VALUE_AGGREGATE) {
                            size_t chunk_index;

                            for (chunk_index = 0U; chunk_index < abi_value->register_chunks;
                                 ++chunk_index) {
                                size_t chunk_offset;

                                chunk_offset = offset + chunk_index * 8U;
                                if (integer_register_index < 8U) {
                                    if (fprintf(file,
                                                "  ld a%zu, %zu(sp)\n",
                                                integer_register_index,
                                                chunk_offset) < 0) {
                                        return false;
                                    }
                                    integer_register_index += 1U;
                                } else {
                                    if (!minic_riscv64_emit_sp_load64(file, "t0", chunk_offset) ||
                                        !minic_riscv64_emit_sp_store64(
                                            file, "t0", stack_argument_index * 8U)) {
                                        return false;
                                    }
                                    stack_argument_index += 1U;
                                }
                            }
                        } else {
                            return false;
                        }
                    } else if (integer_register_index < 8U) {
                        if (fprintf(file, "  ld a%zu, %zu(sp)\n", integer_register_index, offset) <
                            0) {
                            return false;
                        }
                        integer_register_index += 1U;
                    } else {
                        if (!minic_riscv64_emit_sp_load64(file, "t0", offset) ||
                            !minic_riscv64_emit_sp_store64(file, "t0", stack_argument_index * 8U)) {
                            return false;
                        }
                        stack_argument_index += 1U;
                    }
                }
            }
        }

        if (is_indirect) {
            if (staged_bytes < 16U ||
                fprintf(file, "  ld t0, %zu(sp)\n", outgoing_stack_bytes + staged_bytes - 16U) <
                    0) {
                return false;
            }
        }
        temporary_bytes = staged_bytes;
        if (outgoing_stack_bytes == 0U && temporary_bytes != 0U &&
            !minic_riscv64_emit_stack_release(file, temporary_bytes)) {
            return false;
        }
        if (has_indirect_return) {
            size_t result_offset;

            if (outgoing_stack_bytes == 0U) {
                result_offset = 0U;
            } else {
                if (outgoing_stack_bytes > SIZE_MAX - staged_bytes) {
                    return false;
                }
                result_offset = outgoing_stack_bytes + staged_bytes;
            }
            if (result_offset <= 2047U) {
                if (fprintf(file, "  addi a0, sp, %zu\n", result_offset) < 0) {
                    return false;
                }
            } else if (fprintf(file,
                               "  li t1, %zu\n"
                               "  add a0, sp, t1\n",
                               result_offset) < 0) {
                return false;
            }
        }
        if (is_indirect) {
            if (fprintf(file, "  jalr ra, t0, 0\n") < 0) {
                return false;
            }
        } else if (fprintf(file, "  call %s\n", minic_c0_function_symbol_name(direct_callee)) < 0) {
            return false;
        }
        if (outgoing_stack_bytes != 0U) {
            if (temporary_bytes > SIZE_MAX - outgoing_stack_bytes ||
                !minic_riscv64_emit_stack_release(file, temporary_bytes + outgoing_stack_bytes)) {
                return false;
            }
        }

        if (minic_type_is_integer(expression->type)) {
            return minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
        }
        if (minic_type_is_double(expression->type)) {
            return fprintf(file, "  fmv.x.d a0, fa0\n") >= 0;
        }
        if (minic_type_is_record(expression->type)) {
            size_t aggregate_size;
            size_t aggregate_chunks;

            if (has_indirect_return) {
                return true;
            }
            return minic_riscv64_integer_aggregate_abi(
                program, expression->type, &aggregate_size, &aggregate_chunks);
        }
        return minic_type_is_pointer(expression->type) || minic_type_is_void(expression->type);
    }
    }
    return false;
}

bool minic_riscv64_emit_expression(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   const MinicRiscv64FunctionLayout *function_layout,
                                   MinicExpressionId expression_id) {
    return minic_riscv64_emit_expression_impl(
        file, program, function, function_layout, expression_id, 0U);
}
