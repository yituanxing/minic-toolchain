#include "frontend/parser_internal.h"
#include "frontend/parser_runtime_initializer.h"
#include "frontend/parser_local_declarations.h"
#include "frontend/parser_gnu_inline_asm.h"
#include "frontend/parser_labels.h"
#include "frontend/parser_statement_control.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool minic_parser_statement_local_lvalue(MinicParser *parser,
                                        MinicLocalId local_id,
                                        MinicSourceSpan span,
                                        MinicExpressionId *expression_id) {
    const MinicLocal *local;
    MinicExpression expression;

    local = minic_c0_program_local(parser->program, local_id);
    if (local == NULL) {
        minic_parser_error(parser, "invalid local assignment target");
        return false;
    }
    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LOCAL;
    expression.span = span;
    expression.type = local->type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.local_id = local_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

bool minic_parser_statement_modifiable_lvalue(const MinicExpression *expression) {
    return expression != NULL && expression->value_category == MINIC_VALUE_LVALUE &&
           !minic_type_is_const(expression->type);
}

bool minic_parser_statement_assignment_conversion(MinicParser *parser,
                                        MinicType target_type,
                                        MinicExpressionId *expression_id) {
    const MinicExpression *source;
    MinicExpression conversion;
    MinicExpressionId source_id;

    if (parser == NULL || expression_id == NULL) {
        return false;
    }
    source_id = *expression_id;
    source = minic_c0_program_expression(parser->program, source_id);
    if (source == NULL) {
        minic_parser_error(parser, "invalid assignment conversion source");
        return false;
    }
    if (minic_c0_assignment_compatible(parser->program, target_type, source_id)) {
        return true;
    }
    if (minic_type_gnu_discard_const_one_pointer_compatible(target_type, source->type)) {
        /* Preserve a precise cast in the AST. This is the same underlying
         * pointer representation; never silently permit deeper pointer chains or
         * a volatile qualification drop. */
        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = source->span;
        conversion.type = target_type;
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = source_id;
        return minic_parser_add_expression(parser, &conversion, expression_id);
    }
    /* Statement-level assignment/initializer/return conversion must match
       the expression-assignment path. When plain assignment compatibility is
       insufficient, materialize any non-pointer scalar cast that the frontend
       already accepts explicitly. This covers standard arithmetic conversions
       such as double -> unsigned without admitting implicit pointer casts. */
    if (minic_type_is_pointer(target_type) || minic_type_is_pointer(source->type) ||
        !minic_type_cast_compatible(target_type, source->type)) {
        return true;
    }

    (void)memset(&conversion, 0, sizeof(conversion));
    conversion.kind = MINIC_EXPRESSION_CAST;
    conversion.span = source->span;
    conversion.type = target_type;
    conversion.value_category = MINIC_VALUE_RVALUE;
    conversion.value.unary.operand = source_id;
    return minic_parser_add_expression(parser, &conversion, expression_id);
}

static bool add_record_copy_statement(MinicParser *parser,
                                      MinicExpressionId target_id,
                                      MinicExpressionId source_id,
                                      MinicSourceSpan span,
                                      MinicStatementKind statement_kind) {
    const MinicExpression *target;
    const MinicExpression *source;
    const MinicRecord *record;
    MinicStatement statement;
    bool is_initializer;

    is_initializer = statement_kind == MINIC_STATEMENT_RECORD_INITIALIZE;
    if (!is_initializer && statement_kind != MINIC_STATEMENT_RECORD_COPY) {
        return false;
    }
    target = minic_c0_program_expression(parser->program, target_id);
    source = minic_c0_program_expression(parser->program, source_id);
    if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
        !minic_c0_record_value_is_copy_source(parser->program, source_id) ||
        !minic_type_is_record(target->type) || !minic_type_is_record(source->type) ||
        target->type.record_id != source->type.record_id ||
        (!is_initializer && minic_type_is_const(target->type))) {
        minic_parser_error(parser,
                           is_initializer
                               ? "record initializer requires a matching record copy source"
                               : "record assignment requires a matching record copy source");
        return false;
    }
    record = minic_c0_program_record(parser->program, target->type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser,
                           is_initializer ? "record initializer requires a complete record"
                                          : "record assignment requires a complete record");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = statement_kind;
    statement.span = span;
    statement.target_expression = target_id;
    statement.expression = source_id;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_parser_add_statement(parser, &statement);
}

bool minic_parser_statement_record_copy(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span) {
    return add_record_copy_statement(
        parser, target_id, source_id, span, MINIC_STATEMENT_RECORD_COPY);
}

bool minic_parser_statement_record_initialize(MinicParser *parser,
                                        MinicExpressionId target_id,
                                        MinicExpressionId source_id,
                                        MinicSourceSpan span) {
    return add_record_copy_statement(
        parser, target_id, source_id, span, MINIC_STATEMENT_RECORD_INITIALIZE);
}

static bool assignment_chain_expression_is_stable(const MinicParser *parser,
                                                  MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (parser == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, expression->value.local_id);
        return local != NULL && !local->is_array;
    }
    if (expression->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        return assignment_chain_expression_is_stable(parser, expression->value.member.base);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
        return assignment_chain_expression_is_stable(parser, expression->value.unary.operand);
    }
    return false;
}

static bool assignment_chain_target_is_stable(const MinicParser *parser,
                                              const MinicExpression *target) {
    if (parser == NULL || target == NULL || !minic_parser_statement_modifiable_lvalue(target) ||
        minic_type_is_record(target->type)) {
        return false;
    }
    if (target->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicLocal *local;

        local = minic_c0_program_local(parser->program, target->value.local_id);
        return local != NULL && !local->is_array;
    }
    if (target->kind == MINIC_EXPRESSION_GLOBAL_OBJECT) {
        return true;
    }
    return target->kind == MINIC_EXPRESSION_MEMBER &&
           assignment_chain_expression_is_stable(parser, target->value.member.base);
}

static bool parse_stable_assignment_chain_value(MinicParser *parser,
                                                MinicExpressionId target_id,
                                                MinicExpressionId *value_id) {
    const MinicExpression *target;
    const MinicExpression *assigned_expression;
    MinicExpression read;
    MinicExpressionId right_id;
    MinicStatement assignment;
    MinicSourceSpan target_span;
    MinicType target_type;

    if (parser == NULL || value_id == NULL || parser->current.kind != MINIC_TOKEN_EQUAL) {
        return false;
    }
    target = minic_c0_program_expression(parser->program, target_id);
    if (!assignment_chain_target_is_stable(parser, target)) {
        minic_parser_error(parser,
                           "assignment expression currently requires a stable scalar lvalue");
        return false;
    }
    target_span = target->span;
    target_type = target->type;

    if (!minic_parser_advance(parser) || !minic_parser_parse_expression(parser, &right_id, 0U)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        if (!parse_stable_assignment_chain_value(parser, right_id, &right_id)) {
            return false;
        }
    }
    if (!minic_parser_statement_assignment_conversion(parser, target_type, &right_id)) {
        return false;
    }
    assigned_expression = minic_c0_program_expression(parser->program, right_id);
    if (assigned_expression == NULL ||
        !minic_c0_assignment_compatible(parser->program, target_type, right_id)) {
        minic_parser_error(parser, "assignment expression type does not match target type");
        return false;
    }

    (void)memset(&assignment, 0, sizeof(assignment));
    assignment.kind = MINIC_STATEMENT_ASSIGN;
    assignment.span.begin = target_span.begin;
    assignment.span.end = assigned_expression->span.end;
    assignment.target_expression = target_id;
    assignment.expression = right_id;
    assignment.target_statement = MINIC_STATEMENT_INVALID;
    assignment.then_block = MINIC_BLOCK_INVALID;
    assignment.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_add_statement(parser, &assignment)) {
        return false;
    }

    (void)memset(&read, 0, sizeof(read));
    read.kind = MINIC_EXPRESSION_LVALUE_READ;
    read.span = target_span;
    read.type = target_type;
    read.value_category = MINIC_VALUE_RVALUE;
    read.value.unary.operand = target_id;
    return minic_parser_add_expression(parser, &read, value_id);
}

static bool parse_expression_or_assignment_statement(MinicParser *parser,
                                                     bool allow_expression_statement) {
    MinicStatement statement;
    const MinicExpression *first_expression;
    MinicType first_type;
    MinicTokenKind assignment_token;

    (void)memset(&statement, 0, sizeof(statement));
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    first_expression = minic_c0_program_expression(parser->program, statement.expression);
    if (first_expression == NULL) {
        minic_parser_error(parser, "invalid statement expression");
        return false;
    }
    first_type = first_expression->type;
    assignment_token = parser->current.kind;

    if (assignment_token != MINIC_TOKEN_EQUAL && assignment_token != MINIC_TOKEN_CARET_EQUAL &&
        assignment_token != MINIC_TOKEN_PLUS_EQUAL && assignment_token != MINIC_TOKEN_MINUS_EQUAL &&
        assignment_token != MINIC_TOKEN_STAR_EQUAL &&
        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&
        assignment_token != MINIC_TOKEN_PIPE_EQUAL &&
        assignment_token != MINIC_TOKEN_GREATER_GREATER_EQUAL) {
        const MinicExpression *full_expression;

        if (!minic_parser_parse_full_expression_tail(
                parser, statement.expression, &statement.expression)) {
            return false;
        }
        full_expression = minic_c0_program_expression(parser->program, statement.expression);
        if (full_expression == NULL) {
            minic_parser_error(parser, "invalid full expression statement");
            return false;
        }
        if (!allow_expression_statement && full_expression->kind != MINIC_EXPRESSION_ASSIGNMENT) {
            minic_parser_error(parser, "for initializer requires an assignment");
            return false;
        }
        statement.kind = MINIC_STATEMENT_EXPRESSION;
        statement.span.end = full_expression->span.end;
        return minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after expression") &&
               minic_parser_add_statement(parser, &statement);
    }

    statement.kind = assignment_token == MINIC_TOKEN_CARET_EQUAL ? MINIC_STATEMENT_XOR_ASSIGN
                                                                 : MINIC_STATEMENT_ASSIGN;
    statement.target_expression = statement.expression;
    statement.expression = MINIC_EXPRESSION_INVALID;
    if (!minic_parser_statement_modifiable_lvalue(first_expression)) {
        minic_parser_error(parser, "assignment target must be a modifiable lvalue");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    if (assignment_token == MINIC_TOKEN_EQUAL && parser->current.kind == MINIC_TOKEN_EQUAL) {
        if (!parse_stable_assignment_chain_value(
                parser, statement.expression, &statement.expression)) {
            return false;
        }
    }
    if (assignment_token == MINIC_TOKEN_AMPERSAND_EQUAL ||
        assignment_token == MINIC_TOKEN_PIPE_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression bitwise;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_target_info_integer_common_for_program(parser->target_info,
                                                          parser->program,
                                                          first_type,
                                                          right_expression->type,
                                                          &common_type)) {
            minic_parser_error(parser, "compound bitwise assignment requires integer operands");
            return false;
        }
        (void)memset(&bitwise, 0, sizeof(bitwise));
        bitwise.kind = MINIC_EXPRESSION_BINARY;
        bitwise.span.begin = statement.span.begin;
        bitwise.span.end = right_expression->span.end;
        bitwise.type = common_type;
        bitwise.value_category = MINIC_VALUE_RVALUE;
        bitwise.value.binary.operator_kind = assignment_token == MINIC_TOKEN_AMPERSAND_EQUAL
                                                 ? MINIC_BINARY_BITWISE_AND
                                                 : MINIC_BINARY_BITWISE_OR;
        bitwise.value.binary.left = statement.target_expression;
        bitwise.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &bitwise, &statement.expression)) {
            return false;
        }
    }
    if (assignment_token == MINIC_TOKEN_EQUAL && minic_type_is_record(first_type)) {
        const MinicExpression *source;

        source = minic_c0_program_expression(parser->program, statement.expression);
        if (source == NULL || !minic_type_is_record(source->type) ||
            source->type.record_id != first_type.record_id ||
            !minic_parser_statement_record_copy(
                parser, statement.target_expression, statement.expression, source->span)) {
            return false;
        }
        return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
    }
    if (assignment_token == MINIC_TOKEN_PLUS_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression addition;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(
                parser,
                "compound addition assignment requires pointer/integer or integer operands");
            return false;
        }
        if (minic_type_is_pointer(first_type)) {
            MinicType pointee_type;

            if (!minic_type_pointee(first_type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, "pointer update requires a complete object type")) {
                return false;
            }
            common_type = first_type;
        } else if (!minic_type_is_integer(first_type) ||
                   !minic_target_info_integer_common_for_program(parser->target_info,
                                                                 parser->program,
                                                                 first_type,
                                                                 right_expression->type,
                                                                 &common_type)) {
            minic_parser_error(
                parser,
                "compound addition assignment requires pointer/integer or integer operands");
            return false;
        }
        (void)memset(&addition, 0, sizeof(addition));
        addition.kind = MINIC_EXPRESSION_BINARY;
        addition.span.begin = statement.span.begin;
        addition.span.end = right_expression->span.end;
        addition.type = common_type;
        addition.value_category = MINIC_VALUE_RVALUE;
        addition.value.binary.operator_kind = MINIC_BINARY_ADD;
        addition.value.binary.left = statement.target_expression;
        addition.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &addition, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL ||
               assignment_token == MINIC_TOKEN_STAR_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression arithmetic;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(parser,
                               assignment_token == MINIC_TOKEN_STAR_EQUAL
                                   ? "compound multiplication assignment requires integer operands"
                                   : "compound subtraction assignment requires pointer/integer or "
                                     "integer operands");
            return false;
        }
        if (assignment_token == MINIC_TOKEN_MINUS_EQUAL && minic_type_is_pointer(first_type)) {
            MinicType pointee_type;

            if (!minic_type_pointee(first_type, &pointee_type) ||
                !minic_parser_require_complete_object_type(
                    parser, pointee_type, "pointer update requires a complete object type")) {
                return false;
            }
            common_type = first_type;
        } else if (!minic_type_is_integer(first_type) ||
                   !minic_target_info_integer_common_for_program(parser->target_info,
                                                                 parser->program,
                                                                 first_type,
                                                                 right_expression->type,
                                                                 &common_type)) {
            minic_parser_error(parser,
                               assignment_token == MINIC_TOKEN_STAR_EQUAL
                                   ? "compound multiplication assignment requires integer operands"
                                   : "compound subtraction assignment requires pointer/integer or "
                                     "integer operands");
            return false;
        }
        (void)memset(&arithmetic, 0, sizeof(arithmetic));
        arithmetic.kind = MINIC_EXPRESSION_BINARY;
        arithmetic.span.begin = statement.span.begin;
        arithmetic.span.end = right_expression->span.end;
        arithmetic.type = common_type;
        arithmetic.value_category = MINIC_VALUE_RVALUE;
        arithmetic.value.binary.operator_kind = assignment_token == MINIC_TOKEN_STAR_EQUAL
                                                    ? MINIC_BINARY_MULTIPLY
                                                    : MINIC_BINARY_SUBTRACT;
        arithmetic.value.binary.left = statement.target_expression;
        arithmetic.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &arithmetic, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_GREATER_GREATER_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression shift;
        MinicExpressionId right_id;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type)) {
            minic_parser_error(parser, "compound right shift assignment requires integer operands");
            return false;
        }
        (void)memset(&shift, 0, sizeof(shift));
        shift.kind = MINIC_EXPRESSION_BINARY;
        shift.span.begin = statement.span.begin;
        shift.span.end = right_expression->span.end;
        shift.value_category = MINIC_VALUE_RVALUE;
        shift.value.binary.operator_kind = MINIC_BINARY_SHIFT_RIGHT;
        shift.value.binary.left = statement.target_expression;
        shift.value.binary.right = right_id;
        if (!minic_target_info_integer_common_for_program(
                parser->target_info, parser->program, first_type, first_type, &shift.type) ||
            !minic_parser_add_expression(parser, &shift, &statement.expression)) {
            return false;
        }
    }
    if (statement.kind == MINIC_STATEMENT_ASSIGN &&
        !minic_parser_statement_assignment_conversion(parser, first_type, &statement.expression)) {
        return false;
    }
    {
        const MinicExpression *assigned_expression;

        assigned_expression = minic_c0_program_expression(parser->program, statement.expression);
        if (statement.kind == MINIC_STATEMENT_XOR_ASSIGN) {
            MinicType common_type;

            if (assigned_expression == NULL || !minic_type_is_integer(first_type) ||
                !minic_type_is_integer(assigned_expression->type) ||
                !minic_target_info_integer_common_for_program(parser->target_info,
                                                              parser->program,
                                                              first_type,
                                                              assigned_expression->type,
                                                              &common_type)) {
                minic_parser_error(parser, "compound XOR assignment requires integer operands");
                return false;
            }
        } else if (assigned_expression == NULL ||
                   !minic_c0_assignment_compatible(
                       parser->program, first_type, statement.expression)) {
            minic_parser_error(parser, "assignment type does not match target type");
            return false;
        }
        statement.span.end = assigned_expression->span.end;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") &&
           minic_parser_add_statement(parser, &statement);
}

bool minic_parser_parse_compound_statement(MinicParser *parser) {
    MinicCleanupContextId scope_cleanup_context;
    bool success;

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected '{'");
        return false;
    }
    scope_cleanup_context = parser->cleanup_context;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }

    success = minic_parser_advance(parser);
    while (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
            success = false;
            break;
        }
        success = minic_parser_parse_statement(parser, true);
    }
    if (success) {
        success = minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'") &&
                  minic_parser_materialize_cleanup_contexts(parser, scope_cleanup_context);
    }

    minic_parser_end_scope(parser);
    return success;
}

bool minic_parser_parse_statement_expression(MinicParser *parser,
                                             MinicSourcePosition begin,
                                             MinicExpressionId *expression_id) {
    MinicExpression expression;
    MinicBlock *block;
    const MinicExpression *result;
    const MinicStatement *last_statement;
    MinicBlockId block_id;
    MinicBlockId parent_block;
    MinicStatementId last_statement_id;
    bool success;

    if (parser == NULL || expression_id == NULL || parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected '{' in GNU statement expression");
        }
        return false;
    }
    parent_block = parser->current_block;
    if (!minic_c0_program_add_block(parser->program, &block_id) ||
        !minic_parser_begin_scope(parser)) {
        minic_parser_error(parser, "cannot create GNU statement-expression scope");
        return false;
    }
    parser->current_block = block_id;
    parser->statement_expression_depth += 1U;
    success = minic_parser_advance(parser);
    while (success && parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of GNU statement expression");
            success = false;
            break;
        }
        success = minic_parser_parse_statement(parser, true);
    }

    block = block_id < parser->program->block_count ? &parser->program->blocks[block_id] : NULL;
    last_statement = NULL;
    result = NULL;
    last_statement_id = MINIC_STATEMENT_INVALID;
    if (success && block == NULL) {
        minic_parser_error(parser, "invalid GNU statement-expression block");
        success = false;
    }
    if (success && block->statement_count != 0U) {
        last_statement_id = block->statements[block->statement_count - 1U];
        last_statement = minic_c0_program_statement(parser->program, last_statement_id);
        if (last_statement != NULL && last_statement->kind == MINIC_STATEMENT_EXPRESSION &&
            last_statement->expression != MINIC_EXPRESSION_INVALID) {
            result = minic_c0_program_expression(parser->program, last_statement->expression);
            if (result == NULL) {
                minic_parser_error(parser, "invalid GNU statement-expression result");
                success = false;
            }
        }
    }
    if (success) {
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_STATEMENT;
        expression.span.begin = begin;
        expression.span.end = parser->current.span.end;
        expression.type = result == NULL ? minic_type_void() : result->type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.statement_expression.block = block_id;
        expression.value.statement_expression.result =
            result == NULL ? MINIC_EXPRESSION_INVALID : last_statement->expression;
        if (result != NULL) {
            block->statement_count -= 1U;
        }
        success = minic_parser_add_expression(parser, &expression, expression_id) &&
                  minic_parser_expect(
                      parser, MINIC_TOKEN_RBRACE, "expected '}' in GNU statement expression");
    }

    parser->statement_expression_depth -= 1U;
    parser->current_block = parent_block;
    minic_parser_end_scope(parser);
    return success;
}

static bool parse_return(MinicParser *parser) {
    const MinicFunction *function;
    MinicStatement statement;
    MinicType return_type;

    function = minic_c0_program_function(parser->program, parser->current_function);
    if (function == NULL) {
        minic_parser_error(parser, "return statement outside a function");
        return false;
    }

    /* FunctionId is the stable identity. program->functions is a growable arena,
       and parsing a GNU statement-expression may add block-scope extern functions.
       Copy the semantic return type before recursive parsing can reallocate the arena. */
    return_type = function->return_type;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span.begin = parser->current.span.begin;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (minic_type_is_void(return_type)) {
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            statement.span.end = parser->current.span.end;
        } else {
            const MinicExpression *returned_expression;

            if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
                return false;
            }
            returned_expression =
                minic_c0_program_expression(parser->program, statement.expression);
            if (returned_expression == NULL || !minic_type_is_void(returned_expression->type)) {
                minic_parser_error(parser, "void function cannot return a value");
                return false;
            }
            statement.span.end = returned_expression->span.end;
        }
    } else {
        const MinicExpression *returned_expression;

        if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
            minic_parser_error(parser, "non-void function requires a return value");
            return false;
        }
        if (!minic_parser_parse_full_expression(parser, &statement.expression)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            MinicStatement assignment;
            const MinicExpression *assigned_expression;
            const MinicExpression *target;
            MinicExpressionId target_id;

            target_id = statement.expression;
            target = minic_c0_program_expression(parser->program, target_id);
            if (!minic_parser_statement_modifiable_lvalue(target)) {
                minic_parser_error(parser, "assignment target must be a modifiable lvalue");
                return false;
            }
            (void)memset(&assignment, 0, sizeof(assignment));
            assignment.kind = MINIC_STATEMENT_ASSIGN;
            assignment.span.begin = target->span.begin;
            assignment.target_expression = target_id;
            assignment.then_block = MINIC_BLOCK_INVALID;
            assignment.else_block = MINIC_BLOCK_INVALID;
            if (!minic_parser_advance(parser) ||
                !minic_parser_parse_expression(parser, &assignment.expression, 0U) ||
                !minic_parser_statement_assignment_conversion(parser, target->type, &assignment.expression)) {
                return false;
            }
            assigned_expression =
                minic_c0_program_expression(parser->program, assignment.expression);
            if (assigned_expression == NULL ||
                !minic_c0_assignment_compatible(
                    parser->program, target->type, assignment.expression)) {
                minic_parser_error(parser, "assignment type does not match target type");
                return false;
            }
            assignment.span.end = assigned_expression->span.end;
            if (!minic_parser_add_statement(parser, &assignment)) {
                return false;
            }
            statement.expression = target_id;
        }
        if (!minic_parser_statement_assignment_conversion(parser, return_type, &statement.expression)) {
            return false;
        }
        returned_expression = minic_c0_program_expression(parser->program, statement.expression);
        if (returned_expression == NULL ||
            !minic_c0_assignment_compatible(parser->program, return_type, statement.expression)) {
            minic_parser_error(parser, "return expression does not match function return type");
            return false;
        }
        statement.span.end = returned_expression->span.end;
        parser->program->return_expression = statement.expression;
    }

    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") &&
           minic_parser_add_statement(parser, &statement);
}

bool minic_parser_add_default_return(MinicParser *parser) {
    const MinicFunction *function;
    MinicStatement statement;

    function = minic_c0_program_function(parser->program, parser->current_function);
    if (function == NULL) {
        minic_parser_error(parser, "internal error: no active function");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = parser->current.span;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;

    if (minic_type_is_integer(function->return_type)) {
        MinicExpression expression;

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_INTEGER;
        expression.span = parser->current.span;
        expression.type = function->return_type;
        expression.value_category = MINIC_VALUE_RVALUE;
        expression.value.integer_value = 0;
        if (!minic_parser_add_expression(parser, &expression, &statement.expression)) {
            return false;
        }
        parser->program->return_expression = statement.expression;
    } else if (minic_type_is_float(function->return_type) ||
               minic_type_is_double(function->return_type) ||
               minic_type_is_long_double(function->return_type)) {
        MinicExpression zero;
        MinicExpression conversion;
        MinicExpressionId zero_id;

        (void)memset(&zero, 0, sizeof(zero));
        zero.kind = MINIC_EXPRESSION_FLOATING;
        zero.span = parser->current.span;
        zero.type = minic_type_double();
        zero.value_category = MINIC_VALUE_RVALUE;
        zero.value.floating_bits = 0U;
        if (!minic_parser_add_expression(parser, &zero, &zero_id)) {
            return false;
        }

        (void)memset(&conversion, 0, sizeof(conversion));
        conversion.kind = MINIC_EXPRESSION_CAST;
        conversion.span = parser->current.span;
        conversion.type = function->return_type;
        conversion.value_category = MINIC_VALUE_RVALUE;
        conversion.value.unary.operand = zero_id;
        if (!minic_parser_add_expression(parser, &conversion, &statement.expression)) {
            return false;
        }
        parser->program->return_expression = statement.expression;
    } else if (!minic_type_is_void(function->return_type)) {
        minic_parser_error(parser, "unsupported implicit return type");
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

typedef struct MinicStatementAttributeContext {
    bool saw_fallthrough;
    bool saw_informational;
} MinicStatementAttributeContext;

static bool consume_statement_attribute(MinicParser *parser,
                                        const MinicParsedAttribute *attribute,
                                        void *opaque_context) {
    MinicStatementAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicStatementAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL) {
        minic_parser_error(parser, "unsupported GNU statement attribute");
        return false;
    }
    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_STATEMENT)) {
        minic_parser_error(parser, "GNU attribute is not valid on a statement");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        context->saw_informational = true;
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_FALLTHROUGH &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC) {
        context->saw_fallthrough = true;
        return true;
    }
    minic_parser_error(parser, "GNU statement attribute semantics are not implemented");
    return false;
}

static bool gnu_attribute_list_has_statement_shape(const MinicParser *parser) {
    MinicDiagnostic diagnostic;
    MinicParsedAttributeList attributes;
    MinicParser probe;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER ||
        (!minic_parser_current_identifier_lexeme_equals_cstr(parser, "__attribute__") &&
         !minic_parser_current_identifier_lexeme_equals_cstr(parser, "__attribute"))) {
        return false;
    }
    probe = *parser;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    (void)memset(&attributes, 0, sizeof(attributes));
    probe.diagnostic = &diagnostic;
    if (!minic_parser_collect_gnu_attribute_lists(&probe, &attributes)) {
        return false;
    }
    return attributes.count != 0U && probe.current.kind == MINIC_TOKEN_SEMICOLON;
}

static bool parse_gnu_statement_attribute(MinicParser *parser) {
    MinicStatementAttributeContext context;

    (void)memset(&context, 0, sizeof(context));
    if (!minic_parser_parse_gnu_attribute_lists(parser, consume_statement_attribute, &context) ||
        (!context.saw_fallthrough && !context.saw_informational) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU statement attribute")) {
        return false;
    }
    if (context.saw_fallthrough && parser->switch_depth == 0U) {
        minic_parser_error(parser,
                           "GNU fallthrough statement attribute requires an enclosing switch");
        return false;
    }
    return true;
}

static bool block_record_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {
    MinicParser probe;
    size_t brace_depth;

    if (parser == NULL || is_standalone == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_IDENTIFIER && !minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_LBRACE) {
        *is_standalone = false;
        return true;
    }

    brace_depth = 0U;
    do {
        if (probe.current.kind == MINIC_TOKEN_LBRACE) {
            brace_depth += 1U;
        } else if (probe.current.kind == MINIC_TOKEN_RBRACE) {
            if (brace_depth == 0U) {
                return false;
            }
            brace_depth -= 1U;
        }
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    } while (brace_depth != 0U);
    *is_standalone = probe.current.kind == MINIC_TOKEN_SEMICOLON;
    return true;
}

bool minic_parser_parse_statement(MinicParser *parser, bool allow_declaration) {
    if (!minic_parser_label_context_ensure(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return minic_parser_advance(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_PREPROCESSOR_DIRECTIVE) {
        return minic_parser_parse_preprocessor_directive(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        return minic_parser_parse_compound_statement(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {
        if (!allow_declaration) {
            minic_parser_error(parser, "_Static_assert requires a declaration scope");
            return false;
        }
        return minic_parser_parse_static_assert_declaration(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_TYPEDEF) {
        if (!allow_declaration) {
            minic_parser_error(parser, "typedef requires a declaration scope");
            return false;
        }
        return minic_parser_parse_typedef(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_IF) {
        return minic_parser_control_parse_if(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_WHILE) {
        return minic_parser_control_parse_while(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_DO) {
        return minic_parser_control_parse_do_while(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_FOR) {
        return minic_parser_control_parse_for(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_SWITCH) {
        return minic_parser_control_parse_switch(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_CASE) {
        return minic_parser_control_parse_case(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_DEFAULT) {
        return minic_parser_control_parse_default(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_BREAK) {
        return minic_parser_control_parse_break(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_CONTINUE) {
        return minic_parser_control_parse_continue(parser);
    }
    if (minic_parser_gnu_inline_asm_starts(parser)) {
        return minic_parser_gnu_inline_asm_statement(parser);
    }
    if (minic_parser_current_identifier_is_local_label_keyword(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "GNU local label requires a compound statement scope");
            return false;
        }
        return minic_parser_parse_gnu_local_label_declaration(parser);
    }
    if (minic_parser_current_identifier_is_goto(parser)) {
        return minic_parser_parse_goto(parser);
    }
    if (minic_parser_identifier_starts_label(parser)) {
        return minic_parser_parse_label(parser, allow_declaration);
    }
    if (gnu_attribute_list_has_statement_shape(parser)) {
        return parse_gnu_statement_attribute(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_EXTERN) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return minic_parser_local_decl_extern(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_current_identifier_lexeme_equals_cstr(parser, "__attribute__")) {
        MinicParser probe;
        MinicParsedAttributeList prefix_attributes;

        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        probe = *parser;
        (void)memset(&prefix_attributes, 0, sizeof(prefix_attributes));
        if (!minic_parser_collect_gnu_attribute_lists(&probe, &prefix_attributes)) {
            return false;
        }
        /* GNU prefixes qualify ordinary locals as well as externs. The
         * prefix is parsed by the selected declaration owner below, which
         * retains all semantic/ABI attribute validation. */
        if (probe.current.kind == MINIC_TOKEN_KW_EXTERN) {
            return minic_parser_local_decl_extern(parser);
        }
        return minic_parser_local_decl_ordinary(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return minic_parser_local_decl_static(parser);
    }
    if (minic_parser_local_decl_starts_auto_type(parser)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "GNU __auto_type requires a compound statement scope");
            return false;
        }
        return minic_parser_local_decl_auto_type(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_STRUCT ||
        parser->current.kind == MINIC_TOKEN_KW_UNION) {
        bool is_standalone;

        if (!block_record_starts_standalone_declaration(parser, &is_standalone)) {
            return false;
        }
        if (is_standalone) {
            if (!allow_declaration) {
                minic_parser_error(parser, "a declaration requires a compound statement scope");
                return false;
            }
            return minic_parser_parse_record_definition(parser);
        }
    }
    if (minic_parser_token_starts_declaration_specifiers(parser, parser->current)) {
        if (!allow_declaration) {
            minic_parser_error(parser, "a declaration requires a compound statement scope");
            return false;
        }
        return minic_parser_local_decl_ordinary(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_RETURN) {
        return parse_return(parser);
    }
    if (minic_parser_token_starts_expression(parser->current.kind)) {
        return parse_expression_or_assignment_statement(parser, true);
    }
    minic_parser_error(parser,
                       "expected compound, if, while, for, switch, case/default, break, "
                       "goto/label, declaration, expression, return, or '}'");
    return false;
}
