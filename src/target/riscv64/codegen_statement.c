#include "target/riscv64/codegen_internal.h"

static bool minic_riscv64_emit_normalize_word(
    FILE *file,
    MinicType type,
    const char *register_name)
{
    if (!minic_type_is_integer(type)) {
        return false;
    }
    if (minic_type_is_unsigned_integer(type)) {
        return fprintf(
            file,
            "  slli %s, %s, 32\n"
            "  srli %s, %s, 32\n",
            register_name,
            register_name,
            register_name,
            register_name) >= 0;
    }
    return fprintf(
        file,
        "  addiw %s, %s, 0\n",
        register_name,
        register_name) >= 0;
}

static bool minic_riscv64_emit_assignment(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicStatement *statement)
{
    const MinicExpression *target;
    const MinicExpression *value;

    target = minic_c0_program_expression(
        program,
        statement->target_expression);
    value = minic_c0_program_expression(program, statement->expression);
    if (target == NULL || value == NULL ||
        target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_assignment_compatible(target->type, value->type)) {
        return false;
    }
    if (!minic_riscv64_emit_expression(
            file,
            program,
            function,
            statement->expression) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        !minic_riscv64_emit_lvalue_address(
            file,
            program,
            function,
            statement->target_expression) ||
        fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") < 0) {
        return false;
    }
    if (minic_type_is_integer(target->type)) {
        return fprintf(file, "  sw t0, 0(a0)\n") >= 0;
    }
    if (minic_type_is_pointer(target->type)) {
        return fprintf(file, "  sd t0, 0(a0)\n") >= 0;
    }
    return false;
}

static bool minic_riscv64_emit_xor_assignment(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicStatement *statement)
{
    const MinicExpression *target;
    const MinicExpression *value;
    MinicType common_type;

    target = minic_c0_program_expression(
        program,
        statement->target_expression);
    value = minic_c0_program_expression(program, statement->expression);
    if (target == NULL || value == NULL ||
        target->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_integer(target->type) ||
        !minic_type_is_integer(value->type) ||
        !minic_type_integer_common(target->type, value->type, &common_type) ||
        !minic_riscv64_emit_lvalue_address(
            file,
            program,
            function,
            statement->target_expression) ||
        fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
        fprintf(
            file,
            minic_type_is_unsigned_integer(target->type)
                ? "  lwu t0, 0(a0)\n"
                : "  lw t0, 0(a0)\n") < 0 ||
        !minic_riscv64_emit_normalize_word(file, common_type, "t0") ||
        fprintf(file, "  sd t0, 8(sp)\n") < 0 ||
        !minic_riscv64_emit_expression(
            file,
            program,
            function,
            statement->expression) ||
        !minic_riscv64_emit_normalize_word(file, common_type, "a0") ||
        fprintf(
            file,
            "  ld t0, 8(sp)\n"
            "  xor a0, t0, a0\n") < 0 ||
        !minic_riscv64_emit_normalize_word(file, target->type, "a0") ||
        fprintf(
            file,
            "  ld t0, 0(sp)\n"
            "  addi sp, sp, 16\n"
            "  sw a0, 0(t0)\n") < 0) {
        return false;
    }
    return true;
}

static bool minic_riscv64_emit_return(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicStatement *statement)
{
    if (statement->expression == MINIC_EXPRESSION_INVALID) {
        if (!minic_type_is_void(function->return_type)) {
            return false;
        }
    } else {
        const MinicExpression *value;

        value = minic_c0_program_expression(program, statement->expression);
        if (minic_type_is_void(function->return_type) ||
            value == NULL ||
            !minic_type_assignment_compatible(
                function->return_type,
                value->type) ||
            !minic_riscv64_emit_expression(
                file,
                program,
                function,
                statement->expression)) {
            return false;
        }
    }
    return fprintf(file, "  j .L%s_return\n", function->name) >= 0;
}

static bool minic_riscv64_emit_statement(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    const MinicStatement *statement,
    size_t *label_counter)
{
    if (statement == NULL) {
        return false;
    }

    switch (statement->kind) {
    case MINIC_STATEMENT_ASSIGN:
        return minic_riscv64_emit_assignment(
            file,
            program,
            function,
            statement);

    case MINIC_STATEMENT_XOR_ASSIGN:
        return minic_riscv64_emit_xor_assignment(
            file,
            program,
            function,
            statement);

    case MINIC_STATEMENT_EXPRESSION:
        return statement->expression != MINIC_EXPRESSION_INVALID &&
               minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression);

    case MINIC_STATEMENT_RETURN:
        return minic_riscv64_emit_return(
            file,
            program,
            function,
            statement);

    case MINIC_STATEMENT_IF: {
        size_t label;

        label = *label_counter;
        *label_counter += 1U;
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                statement->expression) ||
            fprintf(file, "  beqz a0, .Lif_else_%zu\n", label) < 0 ||
            !minic_riscv64_emit_block(
                file,
                program,
                function,
                statement->then_block,
                label_counter) ||
            fprintf(
                file,
                "  j .Lif_end_%zu\n"
                ".Lif_else_%zu:\n",
                label,
                label) < 0) {
            return false;
        }
        if (statement->else_block != MINIC_BLOCK_INVALID &&
            !minic_riscv64_emit_block(
                file,
                program,
                function,
                statement->else_block,
                label_counter)) {
            return false;
        }
        return fprintf(file, ".Lif_end_%zu:\n", label) >= 0;
    }

    case MINIC_STATEMENT_WHILE: {
        size_t label;

        label = *label_counter;
        *label_counter += 1U;
        return fprintf(file, ".Lwhile_condition_%zu:\n", label) >= 0 &&
               minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression) &&
               fprintf(file, "  beqz a0, .Lwhile_end_%zu\n", label) >= 0 &&
               minic_riscv64_emit_block(
                   file,
                   program,
                   function,
                   statement->then_block,
                   label_counter) &&
               fprintf(
                   file,
                   "  j .Lwhile_condition_%zu\n"
                   ".Lwhile_end_%zu:\n",
                   label,
                   label) >= 0;
    }
    }

    return false;
}

bool minic_riscv64_emit_block(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicBlockId block_id,
    size_t *label_counter)
{
    const MinicBlock *block;
    size_t index;

    block = minic_c0_program_block(program, block_id);
    if (block == NULL) {
        return false;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(
            program,
            block->statements[index]);
        if (!minic_riscv64_emit_statement(
                file,
                program,
                function,
                statement,
                label_counter)) {
            return false;
        }
    }
    return true;
}
