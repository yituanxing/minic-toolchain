#include "target/riscv64/codegen_internal.h"

static bool minic_riscv64_emit_local_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    const MinicLocal *local;
    size_t offset;

    local = minic_c0_program_local(program, local_id);
    if (local == NULL ||
        !minic_riscv64_function_local_offset(
            function,
            local,
            &offset)) {
        return false;
    }
    return fprintf(file, "  addi a0, s0, -%zu\n", offset) >= 0;
}

static bool minic_riscv64_emit_scale_index(
    FILE *file,
    size_t element_size,
    const char *register_name)
{
    size_t shift;
    size_t value;

    if (element_size == 0U) {
        return false;
    }
    shift = 0U;
    value = element_size;
    while (value > 1U && (value % 2U) == 0U) {
        value /= 2U;
        shift += 1U;
    }
    if (value == 1U) {
        if (shift == 0U) {
            return true;
        }
        return fprintf(
            file,
            "  slli %s, %s, %zu\n",
            register_name,
            register_name,
            shift) >= 0;
    }
    return fprintf(
        file,
        "  li t1, %zu\n"
        "  mul %s, %s, t1\n",
        element_size,
        register_name,
        register_name) >= 0;
}

static bool minic_riscv64_emit_subscript_address(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    const MinicExpression *base;
    size_t element_size;

    base = minic_c0_program_expression(
        program,
        expression->value.subscript.base);
    if (base == NULL ||
        !minic_riscv64_type_size(program, expression->type, &element_size)) {
        return false;
    }
    if (minic_type_is_array(base->type)) {
        if (!minic_riscv64_emit_lvalue_address(
                file,
                program,
                function,
                expression->value.subscript.base)) {
            return false;
        }
    } else if (minic_type_is_pointer(base->type)) {
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.subscript.base)) {
            return false;
        }
    } else {
        return false;
    }
    if (fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.subscript.index) ||
        !minic_riscv64_emit_scale_index(file, element_size, "a0") ||
        fprintf(
            file,
            "  ld t0, 0(sp)\n"
            "  addi sp, sp, 16\n"
            "  add a0, t0, a0\n") < 0) {
        return false;
    }
    return true;
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

    base = minic_c0_program_expression(program, expression->value.member.base);
    record = minic_c0_program_record(program, expression->value.member.record_id);
    field = minic_c0_record_field(record, expression->value.member.field_index);
    if (base == NULL || record == NULL || field == NULL) {
        return false;
    }
    if (minic_type_is_pointer(base->type)) {
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.member.base)) {
            return false;
        }
    } else if (minic_type_is_record(base->type)) {
        if (!minic_riscv64_emit_lvalue_address(
                file,
                program,
                function,
                expression->value.member.base)) {
            return false;
        }
    } else {
        return false;
    }
    if (field->storage_offset == 0U) {
        return true;
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
    if (expression == NULL || expression->value_category != MINIC_VALUE_LVALUE) {
        return false;
    }

    switch (expression->kind) {
    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_local_address(
            file,
            program,
            function,
            expression->value.local_id);

    case MINIC_EXPRESSION_GLOBAL_OBJECT: {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(
            program,
            expression->value.global_object_id);
        return object != NULL &&
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

    case MINIC_EXPRESSION_INTEGER:
    case MINIC_EXPRESSION_ADDRESS_OF:
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_UNARY:
    case MINIC_EXPRESSION_BINARY:
    case MINIC_EXPRESSION_CALL:
        return false;
    }

    return false;
}

static bool minic_riscv64_emit_load(
    FILE *file,
    MinicType type)
{
    if (minic_type_is_unsigned_integer(type)) {
        return fprintf(file, "  lwu a0, 0(a0)\n") >= 0;
    }
    if (minic_type_is_signed_integer(type)) {
        return fprintf(file, "  lw a0, 0(a0)\n") >= 0;
    }
    if (minic_type_is_pointer(type)) {
        return fprintf(file, "  ld a0, 0(a0)\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_local_load(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    const MinicLocal *local;

    local = minic_c0_program_local(program, local_id);
    if (local == NULL || local->element_count != 1U ||
        !minic_riscv64_emit_local_address(
            file,
            program,
            function,
            local_id)) {
        return false;
    }
    return minic_riscv64_emit_load(file, local->type);
}

static bool minic_riscv64_emit_unary(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    if (!minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.unary.operand)) {
        return false;
    }
    switch (expression->value.unary.operator_kind) {
    case MINIC_UNARY_PLUS:
        return true;
    case MINIC_UNARY_NEGATE:
        return fprintf(file, "  negw a0, a0\n") >= 0;
    case MINIC_UNARY_LOGICAL_NOT:
        return fprintf(file, "  seqz a0, a0\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_cast(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    const MinicExpression *operand;

    operand = minic_c0_program_expression(
        program,
        expression->value.unary.operand);
    if (operand == NULL ||
        !minic_type_cast_compatible(expression->type, operand->type) ||
        !minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.unary.operand)) {
        return false;
    }
    if (minic_type_is_pointer(expression->type)) {
        return true;
    }
    if (minic_type_is_unsigned_integer(expression->type)) {
        return fprintf(
            file,
            "  slli a0, a0, 32\n"
            "  srli a0, a0, 32\n") >= 0;
    }
    if (minic_type_is_signed_integer(expression->type)) {
        return fprintf(file, "  addiw a0, a0, 0\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_call(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    static const char *const argument_registers[] = {
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7"
    };
    const MinicFunction *callee;
    size_t argument_count;
    size_t spill_size;
    size_t argument_index;

    callee = minic_c0_program_function(
        program,
        expression->value.call.function_id);
    argument_count = expression->value.call.argument_count;
    if (callee == NULL || argument_count > 8U ||
        callee->parameter_count != argument_count) {
        return false;
    }
    spill_size = minic_riscv64_align_up(argument_count * 8U, 16U);
    if (spill_size != 0U &&
        fprintf(file, "  addi sp, sp, -%zu\n", spill_size) < 0) {
        return false;
    }
    for (argument_index = 0U;
         argument_index < argument_count;
         ++argument_index) {
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.call.arguments[argument_index]) ||
            fprintf(file, "  sd a0, %zu(sp)\n", argument_index * 8U) < 0) {
            return false;
        }
    }
    for (argument_index = 0U;
         argument_index < argument_count;
         ++argument_index) {
        if (fprintf(
                file,
                "  ld %s, %zu(sp)\n",
                argument_registers[argument_index],
                argument_index * 8U) < 0) {
            return false;
        }
    }
    if (fprintf(file, "  call %s\n", callee->name) < 0) {
        return false;
    }
    if (spill_size != 0U &&
        fprintf(file, "  addi sp, sp, %zu\n", spill_size) < 0) {
        return false;
    }
    return true;
}

static bool minic_riscv64_emit_pointer_binary(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *left,
    const MinicExpression *right,
    const MinicExpression *expression)
{
    bool pointer_on_left;
    const MinicExpression *integer_expression;
    size_t pointee_size;

    pointer_on_left = minic_type_is_pointer(left->type);
    integer_expression = pointer_on_left ? right : left;
    if (!minic_type_is_integer(integer_expression->type) ||
        !minic_riscv64_pointer_pointee_size(
            program,
            expression->type,
            &pointee_size)) {
        return false;
    }
    if (!minic_riscv64_emit_expression(
            file,
            program,
            function,
            pointer_on_left
                ? expression->value.binary.left
                : expression->value.binary.right) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file,
            program,
            function,
            pointer_on_left
                ? expression->value.binary.right
                : expression->value.binary.left) ||
        !minic_riscv64_emit_scale_index(file, pointee_size, "a0") ||
        fprintf(
            file,
            "  ld t0, 0(sp)\n"
            "  addi sp, sp, 16\n") < 0) {
        return false;
    }
    if (expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        return fprintf(file, "  add a0, t0, a0\n") >= 0;
    }
    if (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
        pointer_on_left) {
        return fprintf(file, "  sub a0, t0, a0\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_integer_binary(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *left,
    const MinicExpression *right,
    const MinicExpression *expression)
{
    bool unsigned_operation;

    unsigned_operation = minic_type_is_unsigned_integer(expression->type) ||
        minic_type_is_unsigned_integer(left->type) ||
        minic_type_is_unsigned_integer(right->type);
    if (!minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.binary.left) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file,
            program,
            function,
            expression->value.binary.right) ||
        fprintf(
            file,
            "  ld t0, 0(sp)\n"
            "  addi sp, sp, 16\n") < 0) {
        return false;
    }

    switch (expression->value.binary.operator_kind) {
    case MINIC_BINARY_ADD:
        return fprintf(file, "  addw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_SUBTRACT:
        return fprintf(file, "  subw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_MULTIPLY:
        return fprintf(file, "  mulw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_DIVIDE:
        return fprintf(
            file,
            unsigned_operation
                ? "  divuw a0, t0, a0\n"
                : "  divw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_REMAINDER:
        return fprintf(
            file,
            unsigned_operation
                ? "  remuw a0, t0, a0\n"
                : "  remw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_SHIFT_LEFT:
        return fprintf(file, "  sllw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_SHIFT_RIGHT:
        return fprintf(
            file,
            minic_type_is_unsigned_integer(left->type)
                ? "  srlw a0, t0, a0\n"
                : "  sraw a0, t0, a0\n") >= 0;
    case MINIC_BINARY_BITWISE_AND:
        return fprintf(file, "  and a0, t0, a0\n") >= 0;
    case MINIC_BINARY_BITWISE_XOR:
        if (fprintf(file, "  xor a0, t0, a0\n") < 0) {
            return false;
        }
        if (minic_type_is_unsigned_integer(expression->type)) {
            return fprintf(
                file,
                "  slli a0, a0, 32\n"
                "  srli a0, a0, 32\n") >= 0;
        }
        return fprintf(file, "  addiw a0, a0, 0\n") >= 0;
    case MINIC_BINARY_EQUAL:
        return fprintf(
            file,
            "  xor a0, t0, a0\n"
            "  seqz a0, a0\n") >= 0;
    case MINIC_BINARY_NOT_EQUAL:
        return fprintf(
            file,
            "  xor a0, t0, a0\n"
            "  snez a0, a0\n") >= 0;
    case MINIC_BINARY_LESS:
        return fprintf(
            file,
            unsigned_operation
                ? "  sltu a0, t0, a0\n"
                : "  slt a0, t0, a0\n") >= 0;
    case MINIC_BINARY_LESS_EQUAL:
        return fprintf(
            file,
            unsigned_operation
                ? "  sltu a0, a0, t0\n  xori a0, a0, 1\n"
                : "  slt a0, a0, t0\n  xori a0, a0, 1\n") >= 0;
    case MINIC_BINARY_GREATER:
        return fprintf(
            file,
            unsigned_operation
                ? "  sltu a0, a0, t0\n"
                : "  slt a0, a0, t0\n") >= 0;
    case MINIC_BINARY_GREATER_EQUAL:
        return fprintf(
            file,
            unsigned_operation
                ? "  sltu a0, t0, a0\n  xori a0, a0, 1\n"
                : "  slt a0, t0, a0\n  xori a0, a0, 1\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_binary(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicExpression *expression)
{
    const MinicExpression *left;
    const MinicExpression *right;

    left = minic_c0_program_expression(
        program,
        expression->value.binary.left);
    right = minic_c0_program_expression(
        program,
        expression->value.binary.right);
    if (left == NULL || right == NULL) {
        return false;
    }
    if (minic_type_is_pointer(left->type) ||
        minic_type_is_pointer(right->type)) {
        return minic_riscv64_emit_pointer_binary(
            file,
            program,
            function,
            left,
            right,
            expression);
    }
    return minic_riscv64_emit_integer_binary(
        file,
        program,
        function,
        left,
        right,
        expression);
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
        return fprintf(file, "  li a0, %d\n", expression->value.integer_value) >= 0;

    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_local_load(
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
        if (!minic_riscv64_emit_lvalue_address(
                file,
                program,
                function,
                expression_id)) {
            return false;
        }
        if (minic_type_is_record(expression->type) ||
            minic_type_is_array(expression->type)) {
            return true;
        }
        return minic_riscv64_emit_load(file, expression->type);

    case MINIC_EXPRESSION_CAST:
        return minic_riscv64_emit_cast(
            file,
            program,
            function,
            expression);

    case MINIC_EXPRESSION_SUBSCRIPT:
        if (!minic_riscv64_emit_subscript_address(
                file,
                program,
                function,
                expression)) {
            return false;
        }
        if (minic_type_is_array(expression->type) ||
            minic_type_is_record(expression->type)) {
            return true;
        }
        return minic_riscv64_emit_load(file, expression->type);

    case MINIC_EXPRESSION_MEMBER:
        if (!minic_riscv64_emit_member_address(
                file,
                program,
                function,
                expression)) {
            return false;
        }
        if (minic_type_is_array(expression->type) ||
            minic_type_is_record(expression->type)) {
            return true;
        }
        return minic_riscv64_emit_load(file, expression->type);

    case MINIC_EXPRESSION_UNARY:
        return minic_riscv64_emit_unary(
            file,
            program,
            function,
            expression);

    case MINIC_EXPRESSION_BINARY:
        return minic_riscv64_emit_binary(
            file,
            program,
            function,
            expression);

    case MINIC_EXPRESSION_CALL:
        return minic_riscv64_emit_call(
            file,
            program,
            function,
            expression);
    }

    return false;
}
