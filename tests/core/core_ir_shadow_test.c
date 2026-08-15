#include "core/core_ir.h"
#include "core/core_lower.h"
#include "frontend/ast.h"
#include "frontend/function_body.h"

#include <stdio.h>
#include <string.h>

static MinicSourceSpan test_span(size_t offset) {
    MinicSourceSpan span;

    (void)memset(&span, 0, sizeof(span));
    span.begin.offset = offset;
    span.begin.line = 1U;
    span.begin.column = offset + 1U;
    span.end = span.begin;
    span.end.offset += 1U;
    span.end.column += 1U;
    return span;
}

static bool add_integer(MinicC0Program *program, int value, MinicExpressionId *expression_id) {
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_local_reference(MinicC0Program *program,
                                MinicLocalId local_id,
                                MinicExpressionId *expression_id) {
    const MinicLocal *local;
    MinicExpression expression;

    local = minic_c0_program_local(program, local_id);
    if (local == NULL) {
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LOCAL;
    expression.span = local->name_span;
    expression.type = local->type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.local_id = local_id;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_binary(MinicC0Program *program,
                       MinicBinaryOperator operator_kind,
                       MinicExpressionId left,
                       MinicExpressionId right,
                       MinicExpressionId *expression_id) {
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BINARY;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.operator_kind = operator_kind;
    expression.value.binary.left = left;
    expression.value.binary.right = right;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool build_return_function(MinicC0Program *program,
                                  MinicBinaryOperator operator_kind,
                                  MinicFunctionBodyView *view) {
    MinicBlockId body_block;
    MinicExpressionId one;
    MinicExpressionId two;
    MinicExpressionId result;
    MinicStatement statement;
    MinicStatementId statement_id;
    MinicFunctionId function_id;

    minic_c0_program_initialize(program);
    if (!minic_c0_program_add_block(program, &body_block) || !add_integer(program, 1, &one) ||
        !add_integer(program, 2, &two) || !add_binary(program, operator_kind, one, two, &result)) {
        return false;
    }
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = test_span(program->statement_count);
    statement.expression = result;
    statement.cleanup_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    if (!minic_c0_program_add_statement(program, &statement, &statement_id) ||
        !minic_c0_block_add_statement(program, body_block, statement_id) ||
        !minic_c0_program_add_function(program, "f", 1U, 0U, 0U, body_block, &function_id) ||
        !minic_c0_program_set_function_signature(
            program, function_id, minic_type_int(), NULL, 0U) ||
        !minic_c0_function_body_view(program, function_id, view)) {
        return false;
    }
    return true;
}

static bool build_parameter_function(MinicC0Program *program, MinicFunctionBodyView *view) {
    MinicBlockId body_block;
    MinicExpressionId local_expression;
    MinicExpressionId one;
    MinicExpressionId result;
    MinicFunctionId function_id;
    MinicLocal local;
    MinicLocalId local_id;
    MinicStatement statement;
    MinicStatementId statement_id;
    MinicType parameter_type;

    minic_c0_program_initialize(program);
    if (!minic_c0_program_add_block(program, &body_block)) {
        return false;
    }
    (void)memset(&local, 0, sizeof(local));
    local.name_span = test_span(0U);
    local.type = minic_type_int();
    local.element_count = 1U;
    if (!minic_c0_program_add_local(program, &local, &local_id) ||
        !add_local_reference(program, local_id, &local_expression) || !add_integer(program, 1, &one) ||
        !add_binary(program, MINIC_BINARY_ADD, local_expression, one, &result)) {
        return false;
    }
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = test_span(program->statement_count);
    statement.expression = result;
    statement.cleanup_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    parameter_type = minic_type_int();
    return minic_c0_program_add_statement(program, &statement, &statement_id) &&
           minic_c0_block_add_statement(program, body_block, statement_id) &&
           minic_c0_program_add_function(program, "f", 1U, 0U, 1U, body_block, &function_id) &&
           minic_c0_program_set_function_signature(
               program, function_id, minic_type_int(), &parameter_type, 1U) &&
           minic_c0_function_body_view(program, function_id, view);
}

static int dump_matches(MinicCoreFunction *core, const char *expected, int failure_base) {
    FILE *output;
    char buffer[512];
    size_t length;
    int status;

    output = tmpfile();
    if (output == NULL || !minic_core_function_dump(output, core) || fflush(output) != 0 ||
        fseek(output, 0L, SEEK_SET) != 0) {
        if (output != NULL) {
            (void)fclose(output);
        }
        return failure_base;
    }
    length = fread(buffer, 1U, sizeof(buffer) - 1U, output);
    status = ferror(output) != 0 ? failure_base + 1 : 0;
    buffer[length] = '\0';
    if (status == 0 && strcmp(buffer, expected) != 0) {
        status = failure_base + 2;
    }
    (void)fclose(output);
    return status;
}

static int test_scalar_shadow_lowering(void) {
    static const char expected[] = "core function @f\n"
                                   "bb0:\n"
                                   "  %0 = const.int 1\n"
                                   "  %1 = const.int 2\n"
                                   "  %2 = add.int %0, %1\n"
                                   "  return %2\n";
    MinicC0Program program;
    MinicFunctionBodyView view;
    MinicCoreFunction core;
    int status;

    if (!build_return_function(&program, MINIC_BINARY_ADD, &view)) {
        minic_c0_program_destroy(&program);
        return 1;
    }
    minic_core_function_initialize(&core);
    if (minic_core_lower_function(&view, &core) != MINIC_CORE_LOWER_OK ||
        !minic_core_function_verify(&core) || core.block_count != 1U ||
        core.instruction_count != 3U || core.value_count != 3U) {
        minic_core_function_destroy(&core);
        minic_c0_program_destroy(&program);
        return 2;
    }
    status = dump_matches(&core, expected, 3);
    minic_core_function_destroy(&core);
    minic_c0_program_destroy(&program);
    return status;
}

static int test_parameter_ingress_lowering(void) {
    static const char expected[] = "core function @f\n"
                                   "object %o0\n"
                                   "bb0:\n"
                                   "  %0 = parameter 0\n"
                                   "  %1 = object.addr %o0\n"
                                   "  store %0, %1\n"
                                   "  %2 = object.addr %o0\n"
                                   "  %3 = load %2\n"
                                   "  %4 = const.int 1\n"
                                   "  %5 = add.int %3, %4\n"
                                   "  return %5\n";
    MinicC0Program program;
    MinicFunctionBodyView view;
    MinicCoreFunction core;
    int status;

    if (!build_parameter_function(&program, &view)) {
        minic_c0_program_destroy(&program);
        return 6;
    }
    minic_core_function_initialize(&core);
    if (minic_core_lower_function(&view, &core) != MINIC_CORE_LOWER_OK ||
        !minic_core_function_verify(&core) || core.object_count != 1U ||
        core.instruction_count != 7U || core.value_count != 6U) {
        minic_core_function_destroy(&core);
        minic_c0_program_destroy(&program);
        return 7;
    }
    status = dump_matches(&core, expected, 8);
    minic_core_function_destroy(&core);
    minic_c0_program_destroy(&program);
    return status;
}

static int test_unsupported_expression_is_distinct(void) {
    MinicC0Program program;
    MinicFunctionBodyView view;
    MinicCoreFunction core;

    if (!build_return_function(&program, MINIC_BINARY_SUBTRACT, &view)) {
        minic_c0_program_destroy(&program);
        return 11;
    }
    minic_core_function_initialize(&core);
    if (minic_core_lower_function(&view, &core) != MINIC_CORE_LOWER_UNSUPPORTED ||
        core.name != NULL || core.block_count != 0U || core.instruction_count != 0U ||
        core.value_count != 0U) {
        minic_core_function_destroy(&core);
        minic_c0_program_destroy(&program);
        return 12;
    }
    minic_core_function_destroy(&core);
    minic_c0_program_destroy(&program);
    return 0;
}

static int test_invalid_input_is_error(void) {
    MinicCoreFunction core;
    int status;

    minic_core_function_initialize(&core);
    status = minic_core_lower_function(NULL, &core) == MINIC_CORE_LOWER_ERROR ? 0 : 13;
    minic_core_function_destroy(&core);
    return status;
}

int main(void) {
    int status;

    status = test_scalar_shadow_lowering();
    if (status != 0) {
        return status;
    }
    status = test_parameter_ingress_lowering();
    if (status != 0) {
        return status;
    }
    status = test_unsupported_expression_is_distinct();
    if (status != 0) {
        return status;
    }
    return test_invalid_input_is_error();
}
