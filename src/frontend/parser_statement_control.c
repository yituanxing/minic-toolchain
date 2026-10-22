#include "frontend/parser_statement_control.h"
#include "frontend/parser_local_declarations.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Structured C statements: condition admission, branch-scope CFG AST,
 * loop break/continue, switch case/default and for-init/update ordering.
 * Keep original construction and cleanup lifetime semantics unchanged. */

static bool parse_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicBlockId parent_block;
    bool success;

    parent_block = parser->current_block;
    if (!minic_c0_program_add_block(parser->program, block_id)) {
        minic_parser_error(parser, "out of memory while adding branch block");
        return false;
    }
    parser->current_block = *block_id;

    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        success = minic_parser_parse_compound_statement(parser);
    } else {
        success = minic_parser_parse_statement(parser, false);
    }

    parser->current_block = parent_block;
    return success;
}

static bool parse_loop_branch(MinicParser *parser, MinicBlockId *block_id) {
    MinicCleanupContextId previous_break_cleanup_context;
    bool success;

    previous_break_cleanup_context = parser->break_cleanup_context;
    parser->break_cleanup_context = parser->cleanup_context;
    parser->loop_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->loop_depth -= 1U;
    parser->break_cleanup_context = previous_break_cleanup_context;
    return success;
}

static bool parse_switch_branch(MinicParser *parser,
                                MinicBlockId *block_id,
                                MinicType selector_type) {
    MinicParserSwitchContext *context;
    MinicCleanupContextId previous_break_cleanup_context;
    bool success;

    if (parser->switch_depth >= MINIC_PARSER_MAX_SWITCH_DEPTH) {
        minic_parser_error(parser, "switch nesting exceeds implementation limit");
        return false;
    }
    /* This is cold state: keep it out of by-value Lexer lookahead copies.
       The owning top-level Parser releases it even on parse failure. */
    if (parser->switch_contexts == NULL) {
        parser->switch_contexts = (MinicParserSwitchContext *)calloc(
            MINIC_PARSER_MAX_SWITCH_DEPTH, sizeof(*parser->switch_contexts));
        if (parser->switch_contexts == NULL) {
            minic_parser_error(parser, "out of memory allocating switch case contexts");
            return false;
        }
    }
    context = &parser->switch_contexts[parser->switch_depth];
    (void)memset(context, 0, sizeof(*context));
    context->selector_type = selector_type;
    previous_break_cleanup_context = parser->break_cleanup_context;
    parser->break_cleanup_context = parser->cleanup_context;
    parser->switch_depth += 1U;
    success = parse_branch(parser, block_id);
    parser->switch_depth -= 1U;
    parser->break_cleanup_context = previous_break_cleanup_context;
    return success;
}

static MinicParserSwitchContext *current_switch_context(MinicParser *parser) {
    if (parser->switch_depth == 0U) {
        return NULL;
    }
    return &parser->switch_contexts[parser->switch_depth - 1U];
}

static bool expression_is_integer_condition(MinicParser *parser, MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL ||
        (!minic_type_is_integer(expression->type) && !minic_type_is_pointer(expression->type))) {
        minic_parser_error(parser, "condition requires an integer or pointer expression");
        return false;
    }
    return true;
}

static bool expression_is_switch_selector(MinicParser *parser, MinicExpressionId expression_id) {
    const MinicExpression *expression;

    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        minic_parser_error(parser, "switch selector requires an integer expression");
        return false;
    }
    return true;
}

bool minic_parser_control_parse_if(MinicParser *parser) {
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_IF;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_integer_condition(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_branch(parser, &statement.then_block)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_KW_ELSE) {
        if (!minic_parser_advance(parser) || !parse_branch(parser, &statement.else_block)) {
            return false;
        }
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool add_internal_continue_label(MinicParser *parser,
                                        MinicSourceSpan span,
                                        MinicStatementId *statement_id) {
    MinicStatement label;

    if (statement_id == NULL) {
        return false;
    }
    (void)memset(&label, 0, sizeof(label));
    label.kind = MINIC_STATEMENT_LABEL;
    label.span = span;
    label.target_expression = MINIC_EXPRESSION_INVALID;
    label.expression = MINIC_EXPRESSION_INVALID;
    label.target_statement = MINIC_STATEMENT_INVALID;
    label.cleanup_context = parser->cleanup_context;
    label.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    label.then_block = MINIC_BLOCK_INVALID;
    label.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_statement(parser->program, &label, statement_id)) {
        minic_parser_error(parser, "out of memory while adding continue target");
        return false;
    }
    return true;
}

bool minic_parser_control_parse_continue(MinicParser *parser) {
    MinicStatement statement;

    if (parser->loop_depth == 0U || parser->continue_target_statement == MINIC_STATEMENT_INVALID) {
        minic_parser_error(parser, "continue statement requires an enclosing loop");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_GOTO;
    statement.span = parser->current.span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = parser->continue_target_statement;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = parser->continue_cleanup_context;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after continue") &&
           minic_parser_add_statement(parser, &statement);
}

bool minic_parser_control_parse_while(MinicParser *parser) {
    MinicStatement statement;
    MinicStatementId continue_label;
    MinicStatementId previous_continue_target;
    MinicCleanupContextId previous_continue_cleanup_context;
    MinicSourceSpan while_span;
    bool success;

    while_span = parser->current.span;
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.span.begin = while_span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_integer_condition(parser, statement.expression) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !add_internal_continue_label(parser, while_span, &continue_label) ||
        !minic_c0_block_add_statement(parser->program, parser->current_block, continue_label)) {
        return false;
    }

    previous_continue_target = parser->continue_target_statement;
    previous_continue_cleanup_context = parser->continue_cleanup_context;
    parser->continue_target_statement = continue_label;
    parser->continue_cleanup_context = parser->cleanup_context;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    parser->continue_cleanup_context = previous_continue_cleanup_context;
    if (!success) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

bool minic_parser_control_parse_do_while(MinicParser *parser) {
    MinicStatement statement;
    MinicStatement condition_check;
    MinicStatement break_statement;
    MinicExpression loop_true;
    MinicExpression negated_condition;
    MinicStatementId continue_label;
    MinicStatementId condition_check_id;
    MinicStatementId break_statement_id;
    MinicStatementId previous_continue_target;
    MinicCleanupContextId previous_continue_cleanup_context;
    MinicBlockId break_block;
    MinicExpressionId condition_id;
    MinicExpressionId negated_condition_id;
    MinicSourceSpan do_span;
    bool success;

    do_span = parser->current.span;
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.span.begin = do_span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    (void)memset(&loop_true, 0, sizeof(loop_true));
    loop_true.kind = MINIC_EXPRESSION_INTEGER;
    loop_true.span = do_span;
    loop_true.type = minic_type_int();
    loop_true.value_category = MINIC_VALUE_RVALUE;
    loop_true.value.integer_value = 1;
    if (!minic_parser_add_expression(parser, &loop_true, &statement.expression) ||
        !add_internal_continue_label(parser, do_span, &continue_label) ||
        !minic_parser_advance(parser)) {
        return false;
    }

    previous_continue_target = parser->continue_target_statement;
    previous_continue_cleanup_context = parser->continue_cleanup_context;
    parser->continue_target_statement = continue_label;
    parser->continue_cleanup_context = parser->cleanup_context;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    parser->continue_cleanup_context = previous_continue_cleanup_context;
    if (!success || parser->current.kind != MINIC_TOKEN_KW_WHILE ||
        !minic_c0_block_add_statement(parser->program, statement.then_block, continue_label)) {
        if (success && parser->current.kind != MINIC_TOKEN_KW_WHILE) {
            minic_parser_error(parser, "expected 'while' after do body");
        }
        return false;
    }

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &condition_id) ||
        !expression_is_integer_condition(parser, condition_id) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after do-while condition") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after do-while")) {
        return false;
    }

    (void)memset(&negated_condition, 0, sizeof(negated_condition));
    negated_condition.kind = MINIC_EXPRESSION_UNARY;
    negated_condition.span = do_span;
    negated_condition.type = minic_type_int();
    negated_condition.value_category = MINIC_VALUE_RVALUE;
    negated_condition.value.unary.operator_kind = MINIC_UNARY_LOGICAL_NOT;
    negated_condition.value.unary.operand = condition_id;
    if (!minic_parser_add_expression(parser, &negated_condition, &negated_condition_id)) {
        return false;
    }

    (void)memset(&break_statement, 0, sizeof(break_statement));
    break_statement.kind = MINIC_STATEMENT_BREAK;
    break_statement.span = do_span;
    break_statement.target_expression = MINIC_EXPRESSION_INVALID;
    break_statement.expression = MINIC_EXPRESSION_INVALID;
    break_statement.target_statement = MINIC_STATEMENT_INVALID;
    break_statement.then_block = MINIC_BLOCK_INVALID;
    break_statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_block(parser->program, &break_block) ||
        !minic_c0_program_add_statement(parser->program, &break_statement, &break_statement_id) ||
        !minic_c0_block_add_statement(parser->program, break_block, break_statement_id)) {
        minic_parser_error(parser, "cannot build do-while exit block");
        return false;
    }

    (void)memset(&condition_check, 0, sizeof(condition_check));
    condition_check.kind = MINIC_STATEMENT_IF;
    condition_check.span = do_span;
    condition_check.target_expression = MINIC_EXPRESSION_INVALID;
    condition_check.expression = negated_condition_id;
    condition_check.target_statement = MINIC_STATEMENT_INVALID;
    condition_check.then_block = break_block;
    condition_check.else_block = MINIC_BLOCK_INVALID;
    if (!minic_c0_program_add_statement(parser->program, &condition_check, &condition_check_id) ||
        !minic_c0_block_add_statement(parser->program, statement.then_block, condition_check_id)) {
        minic_parser_error(parser, "cannot append do-while condition check");
        return false;
    }

    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

bool minic_parser_control_parse_switch(MinicParser *parser) {
    const MinicExpression *selector_expression;
    MinicStatement statement;
    MinicType selector_type;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_SWITCH;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_full_expression(parser, &statement.expression) ||
        !expression_is_switch_selector(parser, statement.expression)) {
        return false;
    }
    selector_expression = minic_c0_program_expression(parser->program, statement.expression);
    if (selector_expression == NULL ||
        !minic_target_info_integer_promotion_for_program(
            parser->target_info, parser->program, selector_expression->type, &selector_type)) {
        minic_parser_error(parser, "cannot determine promoted switch selector type");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parse_switch_branch(parser, &statement.then_block, selector_type)) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool case_integer_constant_value(const MinicParser *parser,
                                        const MinicParserSwitchContext *context,
                                        MinicExpressionId expression_id,
                                        uint64_t *value) {
    MinicConstValue constant;
    MinicConstValue converted;

    if (parser == NULL || context == NULL || parser->program == NULL ||
        parser->target_info == NULL || value == NULL ||
        !minic_const_eval_integer(parser->program, parser->target_info, expression_id, &constant) ||
        !minic_const_value_convert_integer(parser->program,
                                           parser->target_info,
                                           &constant,
                                           context->selector_type,
                                           &converted)) {
        return false;
    }
    if (minic_type_is_signed_integer(context->selector_type)) {
        int64_t signed_value;

        if (!minic_const_value_as_int64(
                parser->program, parser->target_info, &converted, &signed_value)) {
            return false;
        }
        (void)memcpy(value, &signed_value, sizeof(*value));
        return true;
    }
    *value = converted.bits;
    return true;
}

static bool switch_case_value_less(const MinicParserSwitchContext *context,
                                   uint64_t left,
                                   uint64_t right) {
    if (context != NULL && minic_type_is_signed_integer(context->selector_type)) {
        int64_t signed_left;
        int64_t signed_right;

        (void)memcpy(&signed_left, &left, sizeof(signed_left));
        (void)memcpy(&signed_right, &right, sizeof(signed_right));
        return signed_left < signed_right;
    }
    return left < right;
}

static int64_t switch_case_expression_bits(uint64_t value) {
    int64_t result;

    (void)memcpy(&result, &value, sizeof(result));
    return result;
}

bool minic_parser_control_parse_case(MinicParser *parser) {
    MinicParserSwitchContext *context;
    MinicStatement statement;
    const MinicExpression *lower_constant;
    const MinicExpression *upper_constant;
    MinicExpression folded_constant;
    MinicExpressionId lower_expression_id;
    MinicExpressionId upper_expression_id;
    MinicType constant_type;
    uint64_t lower_value;
    uint64_t upper_value;
    size_t index;
    bool is_range;

    context = current_switch_context(parser);
    if (context == NULL) {
        minic_parser_error(parser, "case label requires an enclosing switch");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_CASE;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &lower_expression_id, 0U)) {
        return false;
    }
    lower_constant = minic_c0_program_expression(parser->program, lower_expression_id);
    if (lower_constant == NULL ||
        !case_integer_constant_value(parser, context, lower_expression_id, &lower_value)) {
        minic_parser_error(parser, "case label currently requires an integer constant expression");
        return false;
    }
    constant_type = context->selector_type;
    upper_constant = NULL;
    upper_value = lower_value;
    upper_expression_id = MINIC_EXPRESSION_INVALID;
    is_range = parser->current.kind == MINIC_TOKEN_ELLIPSIS;
    if (is_range) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &upper_expression_id, 0U)) {
            return false;
        }
        upper_constant = minic_c0_program_expression(parser->program, upper_expression_id);
        if (upper_constant == NULL ||
            !case_integer_constant_value(parser, context, upper_expression_id, &upper_value)) {
            minic_parser_error(parser,
                               "GNU case range upper bound must be an integer constant expression");
            return false;
        }
        if (switch_case_value_less(context, upper_value, lower_value)) {
            minic_parser_error(parser, "GNU case range upper bound is below lower bound");
            return false;
        }
    }

    if (context->case_count >= MINIC_PARSER_MAX_SWITCH_CASES) {
        minic_parser_error(parser, "switch case label count exceeds implementation limit");
        return false;
    }
    for (index = 0U; index < context->case_count; ++index) {
        if (!switch_case_value_less(context, context->case_upper_values[index], lower_value) &&
            !switch_case_value_less(context, upper_value, context->case_lower_values[index])) {
            minic_parser_error(parser, "duplicate or overlapping case value range");
            return false;
        }
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after case value")) {
        return false;
    }
    statement.span.end = parser->current.span.begin;

    (void)memset(&folded_constant, 0, sizeof(folded_constant));
    folded_constant.kind = MINIC_EXPRESSION_INTEGER;
    folded_constant.span = lower_constant->span;
    folded_constant.type = constant_type;
    folded_constant.value_category = MINIC_VALUE_RVALUE;
    folded_constant.value.integer_value = switch_case_expression_bits(lower_value);
    if (!minic_parser_add_expression(parser, &folded_constant, &statement.expression)) {
        return false;
    }
    if (is_range) {
        (void)memset(&folded_constant, 0, sizeof(folded_constant));
        folded_constant.kind = MINIC_EXPRESSION_INTEGER;
        if (upper_constant == NULL) {
            minic_parser_error(parser, "invalid GNU case range upper bound");
            return false;
        }
        folded_constant.span = upper_constant->span;
        folded_constant.type = constant_type;
        folded_constant.value_category = MINIC_VALUE_RVALUE;
        folded_constant.value.integer_value = switch_case_expression_bits(upper_value);
        if (!minic_parser_add_expression(parser, &folded_constant, &statement.target_expression)) {
            return false;
        }
    }
    if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }
    context->case_lower_values[context->case_count] = lower_value;
    context->case_upper_values[context->case_count] = upper_value;
    context->case_count += 1U;
    return true;
}

bool minic_parser_control_parse_default(MinicParser *parser) {
    MinicParserSwitchContext *context;
    MinicStatement statement;

    context = current_switch_context(parser);
    if (context == NULL) {
        minic_parser_error(parser, "default label requires an enclosing switch");
        return false;
    }
    if (context->has_default) {
        minic_parser_error(parser, "duplicate default label");
        return false;
    }
    context->has_default = true;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_DEFAULT;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after default")) {
        return false;
    }
    statement.span.end = parser->current.span.begin;
    return minic_parser_add_statement(parser, &statement);
}

static bool parse_for_update(MinicParser *parser, MinicStatementId *statement_id) {
    const MinicExpression *expression;
    MinicStatement statement;

    if (parser == NULL || statement_id == NULL) {
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, statement.expression);
    if (expression == NULL) {
        minic_parser_error(parser, "invalid for update expression");
        return false;
    }
    statement.span = expression->span;
    return minic_c0_program_add_statement(parser->program, &statement, statement_id);
}


static bool parse_for_initializer_expression(MinicParser *parser) {
    MinicStatement statement;
    const MinicExpression *expression;

    if (parser == NULL) {
        return false;
    }
    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, statement.expression);
    if (expression == NULL) {
        minic_parser_error(parser, "invalid for initializer expression");
        return false;
    }
    statement.span.end = expression->span.end;
    return minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after for initializer") &&
           minic_parser_add_statement(parser, &statement);
}

bool minic_parser_control_parse_for(MinicParser *parser) {
    MinicStatement statement;
    MinicStatementId update_statement;
    MinicStatementId continue_label;
    MinicStatementId previous_continue_target;
    MinicCleanupContextId previous_continue_cleanup_context;
    MinicCleanupContextId scope_cleanup_context;
    bool has_update;
    MinicSourceSpan for_span;
    bool success;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_WHILE;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    for_span = parser->current.span;

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('")) {
        return false;
    }
    scope_cleanup_context = parser->cleanup_context;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (minic_parser_token_starts_declaration_specifiers(parser, parser->current)) {
        if (!minic_parser_local_decl_ordinary(parser)) {
            return false;
        }
    } else if (!parse_for_initializer_expression(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_full_expression(parser, &statement.expression) ||
               !expression_is_integer_condition(parser, statement.expression) ||
               !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'")) {
        return false;
    }

    has_update = false;
    update_statement = MINIC_STATEMENT_INVALID;
    if (parser->current.kind != MINIC_TOKEN_RPAREN) {
        if (!parse_for_update(parser, &update_statement)) {
            return false;
        }
        has_update = true;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !add_internal_continue_label(parser, for_span, &continue_label)) {
        return false;
    }
    previous_continue_target = parser->continue_target_statement;
    previous_continue_cleanup_context = parser->continue_cleanup_context;
    parser->continue_target_statement = continue_label;
    parser->continue_cleanup_context = parser->cleanup_context;
    success = parse_loop_branch(parser, &statement.then_block);
    parser->continue_target_statement = previous_continue_target;
    parser->continue_cleanup_context = previous_continue_cleanup_context;
    if (!success ||
        !minic_c0_block_add_statement(parser->program, statement.then_block, continue_label)) {
        if (success) {
            minic_parser_error(parser, "cannot append for-loop continue target");
        }
        return false;
    }
    if (has_update &&
        !minic_c0_block_add_statement(parser->program, statement.then_block, update_statement)) {
        minic_parser_error(parser, "cannot append for-loop update");
        return false;
    }
    statement.span.begin = for_span.begin;
    statement.span.end = parser->current.span.begin;
    success = minic_parser_add_statement(parser, &statement) &&
              minic_parser_materialize_cleanup_contexts(parser, scope_cleanup_context);
    minic_parser_end_scope(parser);
    return success;
}

bool minic_parser_control_parse_break(MinicParser *parser) {
    MinicStatement statement;

    if (parser->loop_depth == 0U && parser->switch_depth == 0U) {
        minic_parser_error(parser, "break statement requires an enclosing loop or switch");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_BREAK;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = parser->break_cleanup_context;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    statement.span.end = parser->current.span.end;
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after break") &&
           minic_parser_add_statement(parser, &statement);
}

