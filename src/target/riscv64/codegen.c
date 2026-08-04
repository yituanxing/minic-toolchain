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

static bool minic_riscv64_emit_local_load(FILE *file, MinicLocalId local_id)
{
    size_t offset;

    if (local_id > SIZE_MAX / 4U) {
        return false;
    }
    offset = local_id * 4U;
    if (offset <= 2047U) {
        return fprintf(file, "  lw a0, %zu(t1)\n", offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, t1, t2\n"
        "  lw a0, 0(t2)\n",
        offset) >= 0;
}

static bool minic_riscv64_emit_local_store(FILE *file, MinicLocalId local_id)
{
    size_t offset;

    if (local_id > SIZE_MAX / 4U) {
        return false;
    }
    offset = local_id * 4U;
    if (offset <= 2047U) {
        return fprintf(file, "  sw a0, %zu(t1)\n", offset) >= 0;
    }
    return fprintf(
        file,
        "  li t2, %zu\n"
        "  add t2, t1, t2\n"
        "  sw a0, 0(t2)\n",
        offset) >= 0;
}

static bool minic_riscv64_emit_expression(
    FILE *file,
    const MinicC0Program *program,
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
            expression->value.local_id);

    case MINIC_EXPRESSION_UNARY:
        if (!minic_riscv64_emit_expression(
                file,
                program,
                expression->value.unary.operand)) {
            return false;
        }
        if (expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
            return fprintf(file, "  negw a0, a0\n") >= 0;
        }
        return true;

    case MINIC_EXPRESSION_BINARY:
        if (!minic_riscv64_emit_expression(
                file,
                program,
                expression->value.binary.left) ||
            fprintf(
                file,
                "  addi sp, sp, -16\n"
                "  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file,
                program,
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
            return fprintf(file, "  slt a0, t0, a0\n") >= 0;
        case MINIC_BINARY_LESS_EQUAL:
            return fprintf(
                file,
                "  slt a0, a0, t0\n"
                "  xori a0, a0, 1\n") >= 0;
        case MINIC_BINARY_GREATER:
            return fprintf(file, "  slt a0, a0, t0\n") >= 0;
        case MINIC_BINARY_GREATER_EQUAL:
            return fprintf(
                file,
                "  slt a0, t0, a0\n"
                "  xori a0, a0, 1\n") >= 0;
        }
        return false;
    }

    return false;
}

static bool minic_riscv64_frame_size(
    const MinicC0Program *program,
    size_t *frame_size)
{
    size_t local_bytes;

    if (program->local_count > (SIZE_MAX - 15U) / 4U) {
        return false;
    }
    local_bytes = program->local_count * 4U;
    *frame_size = (local_bytes + 15U) & ~(size_t)15U;
    return true;
}

static bool minic_riscv64_emit_statements(
    FILE *file,
    const MinicC0Program *program)
{
    size_t index;

    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(program, index);
        if (statement == NULL ||
            !minic_riscv64_emit_expression(
                file,
                program,
                statement->expression)) {
            return false;
        }

        switch (statement->kind) {
        case MINIC_STATEMENT_ASSIGN:
            if (!minic_riscv64_emit_local_store(
                    file,
                    statement->local_id)) {
                return false;
            }
            break;
        case MINIC_STATEMENT_RETURN:
            if (fprintf(file, "  j .Lmain_return\n") < 0) {
                return false;
            }
            break;
        }
    }
    return true;
}

bool minic_riscv64_write_c0_program(
    const char *path,
    const MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    FILE *file;
    size_t frame_size;
    bool success;

    if (!minic_riscv64_frame_size(program, &frame_size)) {
        minic_codegen_set_diagnostic(
            diagnostic,
            path,
            "local frame size exceeds target limits");
        return false;
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

    success = fprintf(
        file,
        ".text\n"
        ".globl main\n"
        ".type main, @function\n"
        "main:\n") >= 0;
    if (success) {
        success = minic_riscv64_emit_stack_allocate(file, frame_size);
    }
    if (success && frame_size != 0U) {
        success = fprintf(file, "  mv t1, sp\n") >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_statements(file, program);
    }
    if (success) {
        success = fprintf(
            file,
            "  li a0, 0\n"
            ".Lmain_return:\n") >= 0;
    }
    if (success) {
        success = minic_riscv64_emit_stack_release(file, frame_size);
    }
    if (success) {
        success = fprintf(
            file,
            "  ret\n"
            ".size main, .-main\n") >= 0;
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
