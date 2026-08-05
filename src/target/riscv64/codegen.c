#include "target/riscv64/codegen.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void minic_codegen_set_diagnostic(
    MinicDiagnostic *diagnostic,
    const char *path,
    const char *message)
{
    if (diagnostic == NULL) {
        return;
    }
    diagnostic->path = path;
    diagnostic->line = 1U;
    diagnostic->column = 1U;
    (void)snprintf(
        diagnostic->message,
        sizeof(diagnostic->message),
        "%s",
        message);
}

static bool minic_riscv64_emit_stack_allocate(FILE *file, size_t size)
{
    if (size == 0U) {
        return true;
    }
    if (size <= 2048U) {
        return fprintf(file, "  addi sp, sp, -%zu\n", size) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  sub sp, sp, t2\n",
        size) >= 0;
}

static bool minic_riscv64_emit_stack_release(FILE *file, size_t size)
{
    if (size == 0U) {
        return true;
    }
    if (size <= 2047U) {
        return fprintf(file, "  addi sp, sp, %zu\n", size) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add sp, sp, t2\n",
        size) >= 0;
}

static bool minic_riscv64_emit_sp_store64(
    FILE *file,
    const char *register_name,
    size_t offset)
{
    if (offset <= 2047U) {
        return fprintf(file, "  sd %s, %zu(sp)\n", register_name, offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, sp, t2\n"
        "  sd %s, 0(t2)\n",
        offset,
        register_name) >= 0;
}

static bool minic_riscv64_emit_sp_load64(
    FILE *file,
    const char *register_name,
    size_t offset)
{
    if (offset <= 2047U) {
        return fprintf(file, "  ld %s, %zu(sp)\n", register_name, offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, sp, t2\n"
        "  ld %s, 0(t2)\n",
        offset,
        register_name) >= 0;
}

static bool minic_riscv64_emit_local_load(
    FILE *file,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    size_t relative_id;
    size_t offset;

    if (local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    relative_id = local_id - function->local_begin;
    if (relative_id > SIZE_MAX / 4U) {
        return false;
    }
    offset = relative_id * 4U;
    if (offset <= 2047U) {
        return fprintf(file, "  lw a0, %zu(s0)\n", offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, s0, t2\n"
        "  lw a0, 0(t2)\n",
        offset) >= 0;
}

static bool minic_riscv64_emit_local_store(
    FILE *file,
    const MinicFunction *function,
    MinicLocalId local_id)
{
    size_t relative_id;
    size_t offset;

    if (local_id < function->local_begin ||
        local_id - function->local_begin >= function->local_count) {
        return false;
    }
    relative_id = local_id - function->local_begin;
    if (relative_id > SIZE_MAX / 4U) {
        return false;
    }
    offset = relative_id * 4U;
    if (offset <= 2047U) {
        return fprintf(file, "  sw a0, %zu(s0)\n", offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, s0, t2\n"
        "  sw a0, 0(t2)\n",
        offset) >= 0;
}

static bool minic_riscv64_emit_expression(
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
        return fprintf(
            file,
            "  li a0, %d\n",
            expression->value.integer_value) >= 0;

    case MINIC_EXPRESSION_LOCAL:
        return minic_riscv64_emit_local_load(
            file,
            function,
            expression->value.local_id);

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
            return true;
        case MINIC_UNARY_NEGATE:
            return fprintf(file, "  negw a0, a0\n") >= 0;
        case MINIC_UNARY_LOGICAL_NOT:
            return fprintf(file, "  seqz a0, a0\n") >= 0;
        }
        return false;

    case MINIC_EXPRESSION_BINARY:
        if (!minic_riscv64_emit_expression(
                file,
                program,
                function,
                expression->value.binary.left) ||
            fprintf(
                file,
                "  addi sp, sp, -16\n"
                "  sd a0, 0(sp)\n") < 0 ||
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
            return fprintf(file, "  divw a0, t0, a0\n") >= 0;
        case MINIC_BINARY_REMAINDER:
            return fprintf(file, "  remw a0, t0, a0\n") >= 0;
        case MINIC_BINARY_EQUAL:
            return fprintf(file, "  xor a0, t0, a0\n  seqz a0, a0\n") >= 0;
        case MINIC_BINARY_NOT_EQUAL:
            return fprintf(file, "  xor a0, t0, a0\n  snez a0, a0\n") >= 0;
        case MINIC_BINARY_LESS:
            return fprintf(file, "  slt a0, t0, a0\n") >= 0;
        case MINIC_BINARY_LESS_EQUAL:
            return fprintf(file, "  slt a0, a0, t0\n  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_GREATER:
            return fprintf(file, "  slt a0, a0, t0\n") >= 0;
        case MINIC_BINARY_GREATER_EQUAL:
            return fprintf(file, "  slt a0, t0, a0\n  xori a0, a0, 1\n") >= 0;
        }
        return false;

    case MINIC_EXPRESSION_CALL: {
        const MinicFunction *callee;

        callee = minic_c0_program_function(
            program,
            expression->value.function_id);
        if (callee == NULL || callee->name_length == 0U) {
            return false;
        }
        return fprintf(file, "  call %s\n", callee->name) >= 0;
    }
    }

    return false;
}

static bool minic_riscv64_frame_size(
    const MinicFunction *function,
    size_t *frame_size)
{
    size_t local_bytes;
    size_t required_bytes;

    if (function->local_count > (SIZE_MAX - 31U) / 4U) {
        return false;
    }
    local_bytes = function->local_count * 4U;
    required_bytes = local_bytes + 16U;
    *frame_size = (required_bytes + 15U) & ~(size_t)15U;
    return true;
}

static bool minic_riscv64_emit_block(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    MinicBlockId block_id,
    size_t *label_counter);

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
        return minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression) &&
               minic_riscv64_emit_local_store(file, function, statement->local_id);

    case MINIC_STATEMENT_RETURN:
        return minic_riscv64_emit_expression(
                   file,
                   program,
                   function,
                   statement->expression) &&
               fprintf(file, "  j .L%s_return\n", function->name) >= 0;

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

static bool minic_riscv64_emit_block(
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

static bool minic_riscv64_emit_function(
    FILE *file,
    const MinicC0Program *program,
    const MinicFunction *function,
    size_t *label_counter)
{
    size_t frame_size;
    bool success;

    if (function == NULL || !function->is_defined ||
        function->name_length == 0U ||
        function->body_block >= program->block_count ||
        !minic_riscv64_frame_size(function, &frame_size)) {
        return false;
    }

    success = fprintf(
        file,
        ".globl %s\n"
        ".type %s, @function\n"
        "%s:\n",
        function->name,
        function->name,
        function->name) >= 0;
    if (success) {
        success = minic_riscv64_emit_stack_allocate(file, frame_size);
    }
    if (success) {
        success = minic_riscv64_emit_sp_store64(file, "ra", frame_size - 8U) &&
                  minic_riscv64_emit_sp_store64(file, "s0", frame_size - 16U) &&
                  fprintf(file, "  mv s0, sp\n") >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_block(
            file,
            program,
            function,
            function->body_block,
            label_counter);
    }
    if (success) {
        success = fprintf(
            file,
            "  li a0, 0\n"
            ".L%s_return:\n",
            function->name) >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_sp_load64(file, "ra", frame_size - 8U) &&
                  minic_riscv64_emit_sp_load64(file, "s0", frame_size - 16U);
    }
    if (success) {
        success = minic_riscv64_emit_stack_release(file, frame_size);
    }
    if (success) {
        success = fprintf(
            file,
            "  ret\n"
            ".size %s, .-%s\n",
            function->name,
            function->name) >= 0;
    }
    return success;
}

bool minic_riscv64_write_c0_program(
    const char *path,
    const MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    FILE *file;
    size_t function_index;
    size_t label_counter;
    bool success;

    {
        const MinicFunction *entry_function;

        entry_function = minic_c0_program_function(
            program,
            program->entry_function);
        if (entry_function == NULL || !entry_function->is_defined ||
            program->function_count == 0U) {
            minic_codegen_set_diagnostic(
                diagnostic,
                path,
                "entry function is missing or invalid");
            return false;
        }
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        char message[256];

        (void)snprintf(
            message,
            sizeof(message),
            "cannot open output: %s",
            strerror(errno));
        minic_codegen_set_diagnostic(diagnostic, path, message);
        return false;
    }

    success = fprintf(file, ".text\n") >= 0;
    label_counter = 0U;
    for (function_index = 0U;
         success && function_index < program->function_count;
         ++function_index) {
        const MinicFunction *function;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            continue;
        }
        success = minic_riscv64_emit_function(
            file,
            program,
            function,
            &label_counter);
    }

    if (!success) {
        minic_codegen_set_diagnostic(
            diagnostic,
            path,
            "cannot write RISC-V assembly");
    }
    if (fclose(file) != 0 && success) {
        minic_codegen_set_diagnostic(
            diagnostic,
            path,
            "cannot close RISC-V assembly output");
        success = false;
    }
    return success;
}
