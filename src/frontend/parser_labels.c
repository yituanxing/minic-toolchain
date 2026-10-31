#include "frontend/parser_labels.h"
#include "frontend/declaration_sema.h"
#include "frontend/initializer.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C labels, forward goto resolution, GNU __label__ lifetime, and asm-goto
 * target binding are one source-language control semantic family.
 * All original label state mutation/order is preserved below. */

bool minic_parser_label_context_ensure(MinicParser *parser) {
    if (parser->current_function == MINIC_FUNCTION_INVALID) {
        minic_parser_error(parser, "goto/label statement outside a function");
        return false;
    }
    if (!parser->label_context_initialized ||
        parser->label_context_function != parser->current_function) {
        parser->label_context_initialized = true;
        parser->label_context_function = parser->current_function;
        parser->function_statement_begin = parser->program->statement_count;
    }
    return true;
}

bool minic_parser_current_identifier_is_goto(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           minic_parser_current_identifier_lexeme_equals_cstr(parser, "goto");
}

static MinicStatementId find_function_label(MinicParser *parser, MinicSourceSpan name_span) {
    size_t statement_index;

    for (statement_index = parser->function_statement_begin;
         statement_index < parser->program->statement_count;
         ++statement_index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(parser->program, statement_index);
        if (statement != NULL && statement->kind == MINIC_STATEMENT_LABEL &&
            !minic_parser_statement_is_local_label(parser, statement_index) &&
            minic_parser_span_equals(parser, statement->span, name_span)) {
            return statement_index;
        }
    }
    return MINIC_STATEMENT_INVALID;
}

MinicStatementId minic_parser_find_label_statement(MinicParser *parser, MinicSourceSpan name_span) {
    MinicStatementId local_label;

    local_label = minic_parser_find_local_label(parser, name_span);
    return local_label != MINIC_STATEMENT_INVALID ? local_label
                                                  : find_function_label(parser, name_span);
}

bool minic_parser_current_identifier_is_local_label_keyword(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_IDENTIFIER &&
           minic_parser_current_identifier_lexeme_equals_cstr(parser, "__label__");
}

bool minic_parser_parse_gnu_local_label_declaration(MinicParser *parser) {
    if (!minic_parser_current_identifier_is_local_label_keyword(parser) || !minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        MinicStatement label;
        MinicStatementId statement_id;
        MinicSourceSpan name_span;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected label name after __label__");
            return false;
        }
        name_span = parser->current.span;
        (void)memset(&label, 0, sizeof(label));
        label.kind = MINIC_STATEMENT_LABEL;
        label.span = name_span;
        label.target_expression = MINIC_EXPRESSION_INVALID;
        label.expression = MINIC_EXPRESSION_INVALID;
        label.target_statement = MINIC_STATEMENT_INVALID;
        label.cleanup_context = parser->cleanup_context;
        label.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
        label.then_block = MINIC_BLOCK_INVALID;
        label.else_block = MINIC_BLOCK_INVALID;
        if (!minic_c0_program_add_statement(parser->program, &label, &statement_id) ||
            !minic_parser_declare_local_label(parser, name_span, statement_id) ||
            !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            break;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU local label declaration");
}

bool minic_parser_parse_goto(MinicParser *parser) {
    MinicStatement statement;
    MinicSourceSpan name_span;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_GOTO;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.cleanup_context = parser->cleanup_context;
    statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_STAR) {
        const MinicExpression *target;

        if (parser->cleanup_context != MINIC_CLEANUP_CONTEXT_ROOT) {
            minic_parser_error(parser, "computed goto with active GNU cleanup is not supported");
            return false;
        }
        statement.span.begin = parser->current.span.begin;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, &statement.expression, 0U)) {
            return false;
        }
        target = minic_c0_program_expression(parser->program, statement.expression);
        if (target == NULL || !minic_type_is_pointer(target->type)) {
            minic_parser_error(parser, "computed goto requires a pointer expression");
            return false;
        }
        statement.span.end = parser->current.span.end;
        return minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after computed goto") &&
               minic_parser_add_statement(parser, &statement);
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected label name after goto");
        return false;
    }
    name_span = parser->current.span;
    statement.span = name_span;
    statement.target_statement = minic_parser_find_label_statement(parser, name_span);
    if (statement.target_statement != MINIC_STATEMENT_INVALID) {
        const MinicStatement *target;

        target = minic_c0_program_statement(parser->program, statement.target_statement);
        if (target == NULL || target->kind != MINIC_STATEMENT_LABEL ||
            !minic_c0_cleanup_context_reaches(
                parser->program, statement.cleanup_context, target->cleanup_context)) {
            minic_parser_error(parser, "goto cannot enter a different GNU cleanup lifetime");
            return false;
        }
        statement.cleanup_stop_context = target->cleanup_context;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after goto")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

bool minic_parser_identifier_starts_label(MinicParser *parser) {
    MinicDiagnostic diagnostic;
    MinicLexer lookahead;
    MinicToken token;

    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER || minic_parser_current_identifier_is_goto(parser)) {
        return false;
    }
    lookahead = parser->lexer;
    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    return minic_lexer_next(&lookahead, &token, &diagnostic) && token.kind == MINIC_TOKEN_COLON;
}

static void resolve_pending_inline_asm_labels(MinicParser *parser,
                                              MinicSourceSpan name_span,
                                              MinicStatementId label_statement_id) {
    size_t statement_index;
    size_t name_length;
    const char *name;

    if (parser == NULL || label_statement_id == MINIC_STATEMENT_INVALID) {
        return;
    }
    name = parser->source + name_span.begin.offset;
    name_length = minic_parser_span_length(name_span);
    for (statement_index = parser->function_statement_begin; statement_index < label_statement_id;
         ++statement_index) {
        const MinicStatement *statement;
        MinicInlineAsm *inline_asm;
        size_t label_index;

        statement = minic_c0_program_statement(parser->program, statement_index);
        if (statement == NULL || statement->kind != MINIC_STATEMENT_INLINE_ASM ||
            statement->inline_asm_id >= parser->program->inline_asm_count) {
            continue;
        }
        inline_asm = &parser->program->inline_asms[statement->inline_asm_id];
        for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
            MinicInlineAsmLabel *label;

            label = &inline_asm->labels[label_index];
            if (label->target_statement == MINIC_STATEMENT_INVALID &&
                label->name_length == name_length && memcmp(label->name, name, name_length) == 0) {
                label->target_statement = label_statement_id;
            }
        }
    }
}

static bool consume_gnu_label_attribute(MinicParser *parser,
                                        const MinicParsedAttribute *attribute,
                                        void *opaque_context) {
    const MinicAttributeDescriptor *descriptor;

    (void)opaque_context;
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_STATEMENT)) {
        minic_parser_error(parser, "unsupported GNU label attribute");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        return true;
    }
    minic_parser_error(parser, "GNU label attribute semantics are not implemented");
    return false;
}

static bool function_label_is_pending(const MinicParser *parser, MinicStatementId statement_id) {
    const MinicStatement *statement;

    statement = parser == NULL ? NULL : minic_c0_program_statement(parser->program, statement_id);
    return statement != NULL && statement->kind == MINIC_STATEMENT_LABEL &&
           statement->target_statement == statement_id;
}

bool minic_parser_parse_label(MinicParser *parser, bool allow_declaration) {
    MinicStatement statement;
    MinicSourceSpan name_span;
    MinicStatementId label_statement_id;
    MinicStatementId local_label_id;
    MinicStatementId existing_function_label_id;
    size_t statement_index;
    bool is_local_label;
    bool defines_pending_function_label;

    name_span = parser->current.span;
    local_label_id = minic_parser_find_local_label(parser, name_span);
    is_local_label = local_label_id != MINIC_STATEMENT_INVALID;
    existing_function_label_id =
        is_local_label ? MINIC_STATEMENT_INVALID : find_function_label(parser, name_span);
    defines_pending_function_label = existing_function_label_id != MINIC_STATEMENT_INVALID &&
                                     function_label_is_pending(parser, existing_function_label_id);
    if (!is_local_label && existing_function_label_id != MINIC_STATEMENT_INVALID &&
        !defines_pending_function_label) {
        minic_parser_error(parser, "duplicate label definition");
        return false;
    }

    if (is_local_label) {
        MinicStatement *local_label;

        if (!minic_parser_define_local_label(parser, name_span, &label_statement_id)) {
            return false;
        }
        local_label = &parser->program->statements[label_statement_id];
        local_label->span = name_span;
        local_label->cleanup_context = parser->cleanup_context;
    } else {
        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_LABEL;
        statement.span = name_span;
        statement.target_expression = MINIC_EXPRESSION_INVALID;
        statement.expression = MINIC_EXPRESSION_INVALID;
        statement.target_statement = MINIC_STATEMENT_INVALID;
        statement.cleanup_context = parser->cleanup_context;
        statement.cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
        statement.then_block = MINIC_BLOCK_INVALID;
        statement.else_block = MINIC_BLOCK_INVALID;
        label_statement_id = defines_pending_function_label ? existing_function_label_id
                                                            : parser->program->statement_count;
    }

    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, "expected ':' after label") ||
        !minic_parser_parse_gnu_attribute_lists(parser, consume_gnu_label_attribute, NULL)) {
        return false;
    }
    if (is_local_label) {
        if (!minic_c0_block_add_statement(
                parser->program, parser->current_block, label_statement_id)) {
            minic_parser_error(parser, "cannot materialize GNU local label definition");
            return false;
        }
    } else if (defines_pending_function_label) {
        parser->program->statements[label_statement_id] = statement;
        if (!minic_c0_block_add_statement(
                parser->program, parser->current_block, label_statement_id)) {
            minic_parser_error(parser, "cannot materialize forward GNU label definition");
            return false;
        }
    } else if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }

    if (!is_local_label) {
        for (statement_index = parser->function_statement_begin;
             statement_index < label_statement_id;
             ++statement_index) {
            MinicStatement *pending;

            pending = &parser->program->statements[statement_index];
            if (pending->kind == MINIC_STATEMENT_GOTO &&
                pending->target_statement == MINIC_STATEMENT_INVALID &&
                minic_parser_span_equals(parser, pending->span, name_span)) {
                if (!minic_c0_cleanup_context_reaches(
                        parser->program, pending->cleanup_context, statement.cleanup_context)) {
                    minic_parser_error(parser,
                                       "goto cannot enter a different GNU cleanup lifetime");
                    return false;
                }
                pending->target_statement = label_statement_id;
                pending->cleanup_stop_context = statement.cleanup_context;
            }
        }
        resolve_pending_inline_asm_labels(parser, name_span, label_statement_id);
    }

    if (parser->current.kind == MINIC_TOKEN_RBRACE || parser->current.kind == MINIC_TOKEN_EOF) {
        minic_parser_error(parser, "label must be followed by a statement");
        return false;
    }
    return minic_parser_parse_statement(parser, allow_declaration);
}

