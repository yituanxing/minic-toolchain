#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"

#include <string.h>

static MinicSourceSpan test_span(size_t offset)
{
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

static bool add_integer(MinicC0Program *program,
                        int value,
                        MinicExpressionId *expression_id)
{
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_cast(MinicC0Program *program,
                     MinicExpressionId operand,
                     MinicType target_type,
                     MinicExpressionId *expression_id)
{
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CAST;
    expression.span = test_span(program->expression_count);
    expression.type = target_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = operand;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static int test_cleanup_expression_remap(void)
{
    MinicC0Program program;
    MinicExpressionId integer_id;
    MinicExpressionId cast_id;
    MinicCleanupContextId cleanup_context_id;
    const MinicCleanupContext *cleanup_context;
    const MinicExpression *normalized;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 257, &integer_id) ||
        !add_cast(&program, integer_id, minic_type_unsigned_char(), &cast_id) ||
        !minic_c0_program_add_cleanup_context(&program,
                                              MINIC_CLEANUP_CONTEXT_ROOT,
                                              cast_id,
                                              &cleanup_context_id) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_normalize_casts(&program) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED)) {
        minic_c0_program_destroy(&program);
        return 1;
    }

    cleanup_context = minic_c0_program_cleanup_context(&program, cleanup_context_id);
    normalized = cleanup_context == NULL
        ? NULL
        : minic_c0_program_expression(&program, cleanup_context->cleanup_expression);
    if (cleanup_context == NULL || normalized == NULL ||
        cleanup_context->cleanup_expression == cast_id ||
        cleanup_context->cleanup_expression != 2U ||
        normalized->kind != MINIC_EXPRESSION_BINARY ||
        normalized->value.binary.operator_kind != MINIC_BINARY_ADD ||
        !minic_type_equal(normalized->type, minic_type_unsigned_char())) {
        minic_c0_program_destroy(&program);
        return 2;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

static int test_failed_normalization_is_transactional(void)
{
    MinicC0Program program;
    MinicExpressionId integer_id;
    MinicExpressionId cast_id;
    MinicCleanupContextId cleanup_context_id;
    MinicExpression *original_expressions;
    size_t original_expression_count;
    MinicExpressionId original_cleanup_expression;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 4, &integer_id) ||
        !add_cast(&program, integer_id, minic_type_unsigned_char(), &cast_id) ||
        !minic_c0_program_add_cleanup_context(&program,
                                              MINIC_CLEANUP_CONTEXT_ROOT,
                                              cast_id,
                                              &cleanup_context_id)) {
        minic_c0_program_destroy(&program);
        return 3;
    }

    program.cleanup_contexts[cleanup_context_id - 1U].cleanup_expression =
        program.expression_count + 10U;
    original_expressions = program.expressions;
    original_expression_count = program.expression_count;
    original_cleanup_expression =
        program.cleanup_contexts[cleanup_context_id - 1U].cleanup_expression;

    if (minic_c0_program_normalize_casts(&program) ||
        program.expressions != original_expressions ||
        program.expression_count != original_expression_count ||
        program.cleanup_contexts[cleanup_context_id - 1U].cleanup_expression !=
            original_cleanup_expression ||
        program.expressions[cast_id].kind != MINIC_EXPRESSION_CAST) {
        minic_c0_program_destroy(&program);
        return 4;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

int main(void)
{
    int status;

    status = test_cleanup_expression_remap();
    if (status != 0) {
        return status;
    }
    return test_failed_normalization_is_transactional();
}
