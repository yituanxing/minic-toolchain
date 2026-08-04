#include "target/riscv64/codegen.h"

#include <errno.h>
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
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
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
        return fprintf(file, "  li a0, %d\n", expression->value.integer_value) >= 0;

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
            fprintf(file, "  addi sp, sp, -16\n  sd a0, 0(sp)\n") < 0 ||
            !minic_riscv64_emit_expression(
                file,
                program,
                expression->value.binary.right) ||
            fprintf(file, "  ld t0, 0(sp)\n  addi sp, sp, 16\n") < 0) {
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
        }
        return false;
    }

    return false;
}

bool minic_riscv64_write_c0_program(
    const char *path,
    const MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    FILE *file;
    bool success;

    file = fopen(path, "wb");
    if (file == NULL) {
        char message[256];

        (void)snprintf(message, sizeof(message), "cannot open output: %s", strerror(errno));
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
        success = minic_riscv64_emit_expression(
            file,
            program,
            program->return_expression);
    }
    if (success) {
        success = fprintf(file, "  ret\n.size main, .-main\n") >= 0;
    }
    if (!success) {
        minic_codegen_set_diagnostic(diagnostic, path, "cannot write RISC-V assembly");
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
