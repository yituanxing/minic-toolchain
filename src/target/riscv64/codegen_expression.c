#include "target/riscv64/codegen_internal.h"

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
            expression->value.call.function_id);
        if (callee == NULL || callee->name_length == 0U ||
            expression->value.call.argument_count != callee->parameter_count ||
            callee->parameter_count > 2U) {
            return false;
        }
        if (callee->parameter_count == 1U) {
            if (!minic_riscv64_emit_expression(
                    file,
                    program,
                    function,
                    expression->value.call.arguments[0])) {
                return false;
            }
        } else if (callee->parameter_count == 2U) {
            if (!minic_riscv64_emit_expression(
                    file,
                    program,
                    function,
                    expression->value.call.arguments[0]) ||
                fprintf(
                    file,
                    "  addi sp, sp, -16\n"
                    "  sd a0, 0(sp)\n") < 0 ||
                !minic_riscv64_emit_expression(
                    file,
                    program,
                    function,
                    expression->value.call.arguments[1]) ||
                fprintf(
                    file,
                    "  mv a1, a0\n"
                    "  ld a0, 0(sp)\n"
                    "  addi sp, sp, 16\n") < 0) {
                return false;
            }
        }
        return fprintf(file, "  call %s\n", callee->name) >= 0;
    }
    }

    return false;
}
