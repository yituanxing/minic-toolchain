#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/layout.h"

#include <inttypes.h>

static bool minic_riscv64_pointer_element_size(const MinicC0Program *program,
                                               MinicType pointer_type,
                                               size_t *element_size) {
    MinicType pointee;
    size_t element_alignment;

    return element_size != NULL && minic_type_pointee(pointer_type, &pointee) &&
           minic_riscv64_type_layout(program, pointee, element_size, &element_alignment);
}

static bool
minic_riscv64_emit_normalize_integer(FILE *file, MinicType type, const char *register_name) {
    return minic_riscv64_emit_integer_conversion(file, type, register_name);
}

static bool minic_riscv64_emit_variadic_argument_conversion(FILE *file, MinicType type) {
    if (minic_type_is_pointer(type)) {
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

static bool minic_riscv64_expression_is_integer_zero(const MinicExpression *expression) {
    return expression != NULL && expression->kind == MINIC_EXPRESSION_INTEGER &&
           minic_type_is_integer(expression->type) && expression->value.integer_value == 0;
}

static bool pointer_equality_shape(const MinicExpression *left, const MinicExpression *right) {
    bool left_is_zero;
    bool right_is_zero;

    if (left == NULL || right == NULL) {
        return false;
    }
    if (minic_type_pointer_equality_compatible(left->type, right->type)) {
        return true;
    }
    left_is_zero = minic_riscv64_expression_is_integer_zero(left);
    right_is_zero = minic_riscv64_expression_is_integer_zero(right);
    return (minic_type_is_pointer(left->type) && right_is_zero) ||
           (left_is_zero && minic_type_is_pointer(right->type));
}

static bool type_is_condition_scalar(MinicType type) {
    return minic_type_is_integer(type) || minic_type_is_pointer(type);
}

static bool minic_riscv64_emit_logical_binary(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
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
        !minic_riscv64_emit_expression(file, program, function, expression->value.binary.left)) {
        return false;
    }

    if (operator_kind == MINIC_BINARY_LOGICAL_AND) {
        return fprintf(file, "  beqz a0, .Lminic_logic_false_%zu\n", expression_id) >= 0 &&
               minic_riscv64_emit_expression(
                   file, program, function, expression->value.binary.right) &&
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
           minic_riscv64_emit_expression(file, program, function, expression->value.binary.right) &&
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

static bool minic_riscv64_emit_double_binary(FILE *file, MinicBinaryOperator operator_kind) {
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

    return fprintf(file,
                   "  fmv.d.x ft0, t0\n"
                   "  fmv.d.x ft1, a0\n"
                   "  %s ft0, ft0, ft1\n"
                   "  fmv.x.d a0, ft0\n",
                   instruction) >= 0;
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

static bool minic_riscv64_emit_subscript_address(FILE *file,
                                                 const MinicC0Program *program,
                                                 const MinicFunction *function,
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

    base_is_array_object = false;
    if (base->kind == MINIC_EXPRESSION_LOCAL && base->value_category == MINIC_VALUE_LVALUE) {
        const MinicLocal *local;

        local = minic_c0_program_local(program, base->value.local_id);
        base_is_array_object = local != NULL && local->element_count > 1U;
        if (base_is_array_object && !minic_type_equal(local->type, expression->type)) {
            return false;
        }
    } else if (base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
               base->value_category == MINIC_VALUE_LVALUE) {
        const MinicGlobalObject *object;
        const MinicArrayType *array_type;

        object = minic_c0_program_global_object(program, base->value.global_object_id);
        if (object == NULL || !minic_type_is_array(object->type)) {
            return false;
        }
        array_type = minic_c0_program_array_type(program, object->type.array_type_id);
        base_is_array_object = array_type != NULL;
        if (!base_is_array_object ||
            !minic_type_equal(array_type->element_type, expression->type)) {
            return false;
        }
    } else if (base->value_category == MINIC_VALUE_LVALUE && minic_type_is_array(base->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(program, base->type.array_type_id);
        base_is_array_object = array_type != NULL;
        if (!base_is_array_object ||
            !minic_type_equal(array_type->element_type, expression->type)) {
            return false;
        }
    }

    if (base_is_array_object) {
        if (!minic_riscv64_emit_lvalue_address(
                file, program, function, expression->value.subscript.base)) {
            return false;
        }
    } else {
        MinicType pointee;

        if (!minic_type_pointee(base->type, &pointee) ||
            !minic_type_equal(pointee, expression->type) ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.subscript.base)) {
            return false;
        }
    }

    if (!minic_riscv64_type_layout(program, expression->type, &element_size, &element_alignment)) {
        return false;
    }

    return fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") >= 0 &&
           minic_riscv64_emit_expression(
               file, program, function, expression->value.subscript.index) &&
           minic_riscv64_emit_scale_register(file, "a0", "t1", element_size) &&
           fprintf(file,
                   "  ld t0, 0(sp)\n"
                   "  addi sp, sp, 16\n"
                   "  add a0, t0, a0\n") >= 0;
}

static bool minic_riscv64_emit_member_address(FILE *file,
                                              const MinicC0Program *program,
                                              const MinicFunction *function,
                                              const MinicExpression *expression) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
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
        !minic_riscv64_emit_expression(file, program, function, expression->value.member.base)) {
        return false;
    }
    if (field->storage_offset == 0U) {
        return true;
    }
    if (field->storage_offset <= 2047U) {
        return fprintf(file, "  addi a0, a0, %zu\n", field->storage_offset) >= 0;
    }
    return fprintf(file,
                   "  li t0, %zu\n"
                   "  add a0, a0, t0\n",
                   field->storage_offset) >= 0;
}

bool minic_riscv64_emit_lvalue_address(FILE *file,
                                       const MinicC0Program *program,
                                       const MinicFunction *function,
                                       MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_address(
            file, program, function, expression->value.local_id);
    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(program, expression->value.global_object_id);
        return object != NULL && object->name_length != 0U &&
               fprintf(file, "  la a0, %s\n", object->name) >= 0;
    }
    case MINIC_EXPRESSION_DEREFERENCE:
        return minic_riscv64_emit_expression(
            file, program, function, expression->value.unary.operand);
    case MINIC_EXPRESSION_SUBSCRIPT:
        return minic_riscv64_emit_subscript_address(file, program, function, expression);
    case MINIC_EXPRESSION_MEMBER:
        return minic_riscv64_emit_member_address(file, program, function, expression);
    default:
        return false;
    }
}

bool minic_riscv64_emit_expression(FILE *file,
                                   const MinicC0Program *program,
                                   const MinicFunction *function,
                                   MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }

    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return fprintf(file, "  li a0, %d\n", expression->value.integer_value) >= 0 &&
               minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
    case MINIC_EXPRESSION_FLOATING:
        return minic_type_is_double(expression->type) &&
               fprintf(file, "  li a0, 0x%016" PRIx64 "\n", expression->value.floating_bits) >= 0;
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_load(file, program, function, expression->value.local_id);
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
        return false;
    case MINIC_EXPRESSION_SIZEOF: {
        size_t alignment;
        size_t size;

        if (!minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            !minic_riscv64_type_layout(
                program, expression->value.sizeof_type, &size, &alignment)) {
            return false;
        }
        return fprintf(file, "  li a0, %zu\n", size) >= 0;
    }
    case MINIC_EXPRESSION_CAST:
        return false;
    case MINIC_EXPRESSION_BITCAST:
        return minic_riscv64_emit_expression(
            file, program, function, expression->value.unary.operand);
    case MINIC_EXPRESSION_ADDRESS_OF:
        return minic_riscv64_emit_lvalue_address(
            file, program, function, expression->value.unary.operand);
    case MINIC_EXPRESSION_DEREFERENCE:
        return minic_riscv64_emit_expression(
                   file, program, function, expression->value.unary.operand) &&
               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_SUBSCRIPT:
        return minic_riscv64_emit_subscript_address(file, program, function, expression) &&
               minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field == NULL ||
            !minic_riscv64_emit_member_address(file, program, function, expression)) {
            return false;
        }
        if (field->element_count > 1U) {
            return minic_type_is_pointer(expression->type);
        }
        return minic_riscv64_emit_scalar_load(file, expression->type, "a0", "a0");
    }
    case MINIC_EXPRESSION_UNARY:
        if (!minic_riscv64_emit_expression(
                file, program, function, expression->value.unary.operand)) {
            return false;
        }
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            return minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_UNARY_NEGATE:
            return fprintf(file,
                           minic_type_is_long_integer(expression->type) ? "  neg a0, a0\n"
                                                                        : "  negw a0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_UNARY_LOGICAL_NOT:
            return fprintf(file, "  seqz a0, a0\n") >= 0;
        }
        return false;
    case MINIC_EXPRESSION_BINARY: {
        const MinicExpression *left;
        const MinicExpression *right;
        MinicType common_integer_type;
        bool has_integer_common_type;
        bool has_pointer_equality;
        size_t element_size;

        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
            expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
            return minic_riscv64_emit_logical_binary(
                file, program, function, expression, expression_id);
        }

        left = minic_c0_program_expression(program, expression->value.binary.left);
        right = minic_c0_program_expression(program, expression->value.binary.right);
        has_integer_common_type =
            left != NULL && right != NULL &&
            minic_type_integer_common(left->type, right->type, &common_integer_type);
        has_pointer_equality = pointer_equality_shape(left, right);
        if (left == NULL || right == NULL ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.binary.left) ||
            (has_integer_common_type &&
             !minic_riscv64_emit_normalize_integer(file, common_integer_type, "a0")) ||
            fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file, program, function, expression->value.binary.right) ||
            (has_integer_common_type &&
             !minic_riscv64_emit_normalize_integer(file, common_integer_type, "a0")) ||
            fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") < 0) {
            return false;
        }
        if (minic_type_is_double(left->type) && minic_type_is_double(right->type) &&
            minic_type_is_double(expression->type)) {
            return minic_riscv64_emit_double_binary(file, expression->value.binary.operator_kind);
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
        case MINIC_BINARY_EQUAL:
            return (has_integer_common_type || has_pointer_equality) &&
                   fprintf(file, "  xor a0, t0, a0\n  seqz a0, a0\n") >= 0;
        case MINIC_BINARY_NOT_EQUAL:
            return (has_integer_common_type || has_pointer_equality) &&
                   fprintf(file, "  xor a0, t0, a0\n  snez a0, a0\n") >= 0;
        case MINIC_BINARY_LESS:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, t0, a0\n"
                               : "  slt a0, t0, a0\n") >= 0;
        case MINIC_BINARY_LESS_EQUAL:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, a0, t0\n  xori a0, a0, 1\n"
                               : "  slt a0, a0, t0\n  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_GREATER:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, a0, t0\n"
                               : "  slt a0, a0, t0\n") >= 0;
        case MINIC_BINARY_GREATER_EQUAL:
            return has_integer_common_type &&
                   fprintf(file,
                           minic_type_is_unsigned_integer(common_integer_type)
                               ? "  sltu a0, t0, a0\n  xori a0, a0, 1\n"
                               : "  slt a0, t0, a0\n  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
            return false;
        }
        return false;
    }
    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *callee;
        size_t argument_count;
        size_t argument_index;
        size_t temporary_bytes;

        callee = minic_c0_program_function(program, expression->value.call.function_id);
        argument_count = expression->value.call.argument_count;
        if (callee == NULL || callee->name_length == 0U || callee->parameter_count > 8U ||
            argument_count < callee->parameter_count || argument_count > 8U ||
            (!callee->is_variadic && argument_count != callee->parameter_count)) {
            return false;
        }
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            const MinicExpression *argument;

            argument = minic_c0_program_expression(
                program, expression->value.call.arguments[argument_index]);
            if (argument == NULL ||
                !minic_riscv64_emit_expression(
                    file, program, function, expression->value.call.arguments[argument_index])) {
                return false;
            }
            if (argument_index < callee->parameter_count) {
                if (minic_type_is_integer(callee->parameter_types[argument_index]) &&
                    !minic_riscv64_emit_integer_conversion(
                        file, callee->parameter_types[argument_index], "a0")) {
                    return false;
                }
            } else if (!minic_riscv64_emit_variadic_argument_conversion(file, argument->type)) {
                return false;
            }
            if (fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0) {
                return false;
            }
        }
        for (argument_index = 0U; argument_index < argument_count; ++argument_index) {
            size_t offset;

            offset = (argument_count - 1U - argument_index) * 16U;
            if (fprintf(file, "  ld a%zu, %zu(sp)\n", argument_index, offset) < 0) {
                return false;
            }
        }
        temporary_bytes = argument_count * 16U;
        if (temporary_bytes != 0U && fprintf(file, "  addi sp, sp, %zu\n", temporary_bytes) < 0) {
            return false;
        }
        if (fprintf(file, "  call %s\n", callee->name) < 0) {
            return false;
        }
        if (minic_type_is_integer(expression->type)) {
            return minic_riscv64_emit_integer_conversion(file, expression->type, "a0");
        }
        if (minic_type_is_double(expression->type)) {
            return fprintf(file, "  fmv.x.d a0, fa0\n") >= 0;
        }
        return minic_type_is_pointer(expression->type) || minic_type_is_void(expression->type);
    }
    }
    return false;
}
