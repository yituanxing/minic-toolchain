#include "target/riscv64/codegen_internal.h"
#include "target/riscv64/layout.h"

static bool minic_riscv64_pointer_shift(
    MinicType pointer_type,
    unsigned int *shift)
{
    MinicType pointee;

    if (shift == NULL || !minic_type_pointee(pointer_type, &pointee)) {
        return false;
    }
    if (minic_type_is_integer(pointee)) {
        *shift = 2U;
        return true;
    }
    if (minic_type_is_pointer(pointee)) {
        *shift = 3U;
        return true;
    }
    return false;
}

static bool minic_riscv64_emit_zero_extend_word(
    FILE *file,
    const char *register_name)
{
    return fprintf(
        file,
        "  slli %s, %s, 32\n"
        "  srli %s, %s, 32\n",
        register_name,
        register_name,
        register_name,
        register_name) >= 0;
}

static bool minic_riscv64_emit_normalize_integer(
    FILE *file,
    MinicType type,
    const char *register_name)
{
    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_unsigned_integer(type)) {
        return minic_riscv64_emit_zero_extend_word(file, register_name);
    }
    return true;
}

static bool minic_riscv64_emit_scale_a0(FILE *file, size_t element_size)
{
    size_t value;
    unsigned int shift;

    if (element_size == 0U) {
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
        return fprintf(file, "  slli a0, a0, %u\n", shift) >= 0;
    }
    return fprintf(
        file,
        "  li t1, %zu\n"
        "  mul a0, a0, t1\n",
        element_size) >= 0;
}

static bool minic_riscv64_emit_subscript_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    const MinicExpression *base;
    const MinicExpression *index;
    bool base_is_array_object;
    size_t element_size;
    size_t element_alignment;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_SUBSCRIPT) {
        return false;
    }
    base = minic_c0_program_expression(
        program,
        expression->value.subscript.base);
    index = minic_c0_program_expression(
        program,
        expression->value.subscript.index);
    if (base == NULL || index == NULL ||
        !minic_type_is_integer(index->type)) {
        return false;
    }

    base_is_array_object = false;
    if (base->kind == MINIC_EXPRESSION_LOCAL &&
        base->value_category == MINIC_VALUE_LVALUE) {
        const MinicLocal *local;

        local = minic_c0_program_local(program, base->value.local_id);
        base_is_array_object = local != NULL && local->element_count > 1U;
        if (base_is_array_object &&
            !minic_type_equal(local->type, expression->type)) {
            return false;
        }
    } else if (base->kind == MINIC_EXPRESSION_GLOBAL_OBJECT &&
               base->value_category == MINIC_VALUE_LVALUE) {
        const MinicGlobalObject *object;
        const MinicArrayType *array_type;

        object = minic_c0_program_global_object(
            program,
            base->value.global_object_id);
        if (object == NULL || !minic_type_is_array(object->type)) {
            return false;
        }
        array_type = minic_c0_program_array_type(
            program,
            object->type.array_type_id);
        base_is_array_object = array_type != NULL;
        if (!base_is_array_object ||
            !minic_type_equal(array_type->element_type, expression->type)) {
            return false;
        }
    } else if (base->value_category == MINIC_VALUE_LVALUE &&
               minic_type_is_array(base->type)) {
        const MinicArrayType *array_type;

        array_type = minic_c0_program_array_type(
            program,
            base->type.array_type_id);
        base_is_array_object = array_type != NULL;
        if (!base_is_array_object ||
            !minic_type_equal(array_type->element_type, expression->type)) {
            return false;
        }
    }

    if (base_is_array_object) {
        if (!minic_riscv64_emit_lvalue_address(
                file,
                program,
                function,
                expression->value.subscript.base)) {
            return false;
        }
    } else {
        MinicType pointee;

        if (!minic_type_pointee(base->type, &pointee) ||
            !minic_type_equal(pointee, expression->type) ||
            !minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.subscript.base)) {
            return false;
        }
    }

    if (!minic_riscv64_type_layout(
            program,
            expression->type,
            &element_size,
            &element_alignment)) {
        return false;
    }

    return fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") >= 0 &&
           minic_riscv64_emit_expression(
               file,
               program,
               function,
               expression->value.subscript.index) &&
           minic_riscv64_emit_scale_a0(file, element_size) &&
           fprintf(
               file,
               "  ld t0, 0(sp)\n"
               "  addi sp, sp, 16\n"
               "  add a0, t0, a0\n") >= 0;
}

static bool minic_riscv64_emit_member_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicType record_type;

    if (expression == NULL || expression->kind != MINIC_EXPRESSION_MEMBER) {
        return false;
    }
    base = minic_c0_program_expression(
        program,
        expression->value.member.base);
    record = minic_c0_program_record(
        program,
        expression->value.member.record_id);
    field = minic_c0_record_field(
        record,
        expression->value.member.field_index);
    if (base == NULL || record == NULL || field == NULL ||
        !minic_type_pointee(base->type, &record_type) ||
        !minic_type_is_record(record_type) ||
        record_type.record_id != expression->value.member.record_id ||
        !minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.member.base)) {
        return false;
    }
    if (field->storage_offset == 0U) {
        return true;
    }
    if (field->storage_offset <= 2047U) {
        return fprintf(
            file,
            "  addi a0, a0, %zu\n",
            field->storage_offset) >= 0;
    }
    return fprintf(
        file,
        "  li t0, %zu\n"
        "  add a0, a0, t0\n",
        field->storage_offset) >= 0;
}

bool minic_riscv64_emit_lvalue_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicExpressionId expression_id)
{
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL ||
        expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }
    switch (expression->kind) {
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_address(
            file,
            program,
            function,
            expression->value.local_id);
    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(
            program,
            expression->value.global_object_id);
        return object != NULL && object->name_length != 0U &&
               fprintf(file, "  la a0, %s\n", object->name) >= 0;
    }
    case MINIC_EXPRESSION_DEREFERENCE:
        return minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.unary.operand);
    case MINIC_EXPRESSION_SUBSCRIPT:
        return minic_riscv64_emit_subscript_address(
            file,
            program,
            function,
            expression);
    case MINIC_EXPRESSION_MEMBER:
        return minic_riscv64_emit_member_address(
            file,
            program,
            function,
            expression);
    default:
        return false;
    }
}

bool minic_riscv64_emit_expression(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicExpressionId expression_id)
{
    const MinicExpression *expression;

    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL) {
        return false;
    }

    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        return fprintf(file, "  li a0, %d\n", expression->value.integer_value) >= 0 &&
               (!minic_type_is_unsigned_integer(expression->type) ||
                minic_riscv64_emit_zero_extend_word(file, "a0"));
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_object_load(
            file,
            program,
            function,
            expression->value.local_id);
    case MINIC_EXPRESSION_GLOBAL_OBJECT:
        return false;
    case MINIC_EXPRESSION_ADDRESS_OF:
        return minic_riscv64_emit_lvalue_address(
            file,
            program,
            function,
            expression->value.unary.operand);
    case MINIC_EXPRESSION_DEREFERENCE:
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.unary.operand)) {
            return false;
        }
        if (minic_type_is_integer(expression->type)) {
            return fprintf(
                file,
                minic_type_is_unsigned_integer(expression->type)
                    ? "  lwu a0, 0(a0)\n"
                    : "  lw a0, 0(a0)\n") >= 0;
        }
        if (minic_type_is_pointer(expression->type)) {
            return fprintf(file, "  ld a0, 0(a0)\n") >= 0;
        }
        return false;
    case MINIC_EXPRESSION_SUBSCRIPT:
        if (!minic_riscv64_emit_subscript_address(
                file,
                program,
                function,
                expression)) {
            return false;
        }
        if (minic_type_is_integer(expression->type)) {
            return fprintf(
                file,
                minic_type_is_unsigned_integer(expression->type)
                    ? "  lwu a0, 0(a0)\n"
                    : "  lw a0, 0(a0)\n") >= 0;
        }
        if (minic_type_is_pointer(expression->type)) {
            return fprintf(file, "  ld a0, 0(a0)\n") >= 0;
        }
        return false;
    case MINIC_EXPRESSION_MEMBER: {
        const MinicRecord *record;
        const MinicRecordField *field;

        record = minic_c0_program_record(
            program,
            expression->value.member.record_id);
        field = minic_c0_record_field(
            record,
            expression->value.member.field_index);
        if (field == NULL ||
            !minic_riscv64_emit_member_address(
                file,
                program,
                function,
                expression)) {
            return false;
        }
        if (field->element_count > 1U) {
            return minic_type_is_pointer(expression->type);
        }
        if (minic_type_is_integer(expression->type)) {
            return fprintf(
                file,
                minic_type_is_unsigned_integer(expression->type)
                    ? "  lwu a0, 0(a0)\n"
                    : "  lw a0, 0(a0)\n") >= 0;
        }
        if (minic_type_is_pointer(expression->type)) {
            return fprintf(file, "  ld a0, 0(a0)\n") >= 0;
        }
        return false;
    }
    case MINIC_EXPRESSION_UNARY:
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.unary.operand)) {
            return false;
        }
        switch (expression->value.unary.operator_kind) {
        case MINIC_UNARY_PLUS:
            return minic_riscv64_emit_normalize_integer(
                file,
                expression->type,
                "a0");
        case MINIC_UNARY_NEGATE:
            return fprintf(file, "  negw a0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_UNARY_LOGICAL_NOT:
            return fprintf(file, "  seqz a0, a0\n") >= 0;
        }
        return false;
    case MINIC_EXPRESSION_BINARY: {
        const MinicExpression *left;
        const MinicExpression *right;
        MinicType common_integer_type;
        bool has_integer_common_type;
        unsigned int shift;

        left = minic_c0_program_expression(
            program,
            expression->value.binary.left);
        right = minic_c0_program_expression(
            program,
            expression->value.binary.right);
        has_integer_common_type = left != NULL && right != NULL &&
            minic_type_integer_common(
                left->type,
                right->type,
                &common_integer_type);
        if (left == NULL || right == NULL ||
            !minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.binary.left) ||
            (has_integer_common_type &&
             !minic_riscv64_emit_normalize_integer(
                 file,
                 common_integer_type,
                 "a0")) ||
            fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.binary.right) ||
            (has_integer_common_type &&
             !minic_riscv64_emit_normalize_integer(
                 file,
                 common_integer_type,
                 "a0")) ||
            fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") < 0) {
            return false;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            if (has_integer_common_type) {
                return fprintf(file, "  addw a0, t0, a0\n") >= 0 &&
                       minic_riscv64_emit_normalize_integer(
                           file,
                           expression->type,
                           "a0");
            }
            if (minic_type_is_pointer(left->type) &&
                minic_type_is_integer(right->type) &&
                minic_riscv64_pointer_shift(left->type, &shift)) {
                return fprintf(
                    file,
                    "  slli a0, a0, %u\n"
                    "  add a0, t0, a0\n",
                    shift) >= 0;
            }
            if (minic_type_is_integer(left->type) &&
                minic_type_is_pointer(right->type) &&
                minic_riscv64_pointer_shift(right->type, &shift)) {
                return fprintf(
                    file,
                    "  slli t0, t0, %u\n"
                    "  add a0, a0, t0\n",
                    shift) >= 0;
            }
            return false;
        case MINIC_BINARY_SUBTRACT:
            if (has_integer_common_type) {
                return fprintf(file, "  subw a0, t0, a0\n") >= 0 &&
                       minic_riscv64_emit_normalize_integer(
                           file,
                           expression->type,
                           "a0");
            }
            if (minic_type_is_pointer(left->type) &&
                minic_type_is_integer(right->type) &&
                minic_riscv64_pointer_shift(left->type, &shift)) {
                return fprintf(
                    file,
                    "  slli a0, a0, %u\n"
                    "  sub a0, t0, a0\n",
                    shift) >= 0;
            }
            return false;
        case MINIC_BINARY_MULTIPLY:
            return has_integer_common_type &&
                   fprintf(file, "  mulw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_DIVIDE:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(common_integer_type)
                           ? "  divuw a0, t0, a0\n"
                           : "  divw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_REMAINDER:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(common_integer_type)
                           ? "  remuw a0, t0, a0\n"
                           : "  remw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_SHIFT_LEFT:
            return has_integer_common_type &&
                   fprintf(file, "  sllw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_SHIFT_RIGHT:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(left->type)
                           ? "  srlw a0, t0, a0\n"
                           : "  sraw a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_BITWISE_AND:
            return has_integer_common_type &&
                   fprintf(file, "  and a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_BITWISE_XOR:
            return has_integer_common_type &&
                   fprintf(file, "  xor a0, t0, a0\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(
                       file,
                       expression->type,
                       "a0");
        case MINIC_BINARY_EQUAL:
            return has_integer_common_type &&
                   fprintf(file, "  xor a0, t0, a0\n  seqz a0, a0\n") >= 0;
        case MINIC_BINARY_NOT_EQUAL:
            return has_integer_common_type &&
                   fprintf(file, "  xor a0, t0, a0\n  snez a0, a0\n") >= 0;
        case MINIC_BINARY_LESS:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(common_integer_type)
                           ? "  sltu a0, t0, a0\n"
                           : "  slt a0, t0, a0\n") >= 0;
        case MINIC_BINARY_LESS_EQUAL:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(common_integer_type)
                           ? "  sltu a0, a0, t0\n  xori a0, a0, 1\n"
                           : "  slt a0, a0, t0\n  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_GREATER:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(common_integer_type)
                           ? "  sltu a0, a0, t0\n"
                           : "  slt a0, a0, t0\n") >= 0;
        case MINIC_BINARY_GREATER_EQUAL:
            return has_integer_common_type &&
                   fprintf(
                       file,
                       minic_type_is_unsigned_integer(common_integer_type)
                           ? "  sltu a0, t0, a0\n  xori a0, a0, 1\n"
                           : "  slt a0, t0, a0\n  xori a0, a0, 1\n") >= 0;
        }
        return false;
    }
    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *callee;
        size_t argument_index;
        size_t temporary_bytes;

        callee = minic_c0_program_function(
            program,
            expression->value.call.function_id);
        if (callee == NULL || callee->name_length == 0U ||
            expression->value.call.argument_count != callee->parameter_count ||
            callee->parameter_count > 8U) {
            return false;
        }
        for (argument_index = 0U;
             argument_index < callee->parameter_count;
             ++argument_index) {
            if (!minic_riscv64_emit_expression(
                    file,
                    program,
                    function,
                    expression->value.call.arguments[argument_index]) ||
                fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0) {
                return false;
            }
        }
        for (argument_index = 0U;
             argument_index < callee->parameter_count;
             ++argument_index) {
            size_t offset;

            offset = (callee->parameter_count - 1U - argument_index) * 16U;
            if (fprintf(file, "  ld a%zu, %zu(sp)\n", argument_index, offset) < 0) {
                return false;
            }
        }
        temporary_bytes = callee->parameter_count * 16U;
        if (temporary_bytes != 0U &&
            fprintf(file, "  addi sp, sp, %zu\n", temporary_bytes) < 0) {
            return false;
        }
        if (fprintf(file, "  call %s\n", callee->name) < 0) {
            return false;
        }
        return !minic_type_is_unsigned_integer(expression->type) ||
               minic_riscv64_emit_zero_extend_word(file, "a0");
    }
    }
    return false;
}
