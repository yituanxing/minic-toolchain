#include "frontend/parser_gnu_inline_asm.h"
#include "frontend/expression_semantics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* The entire GNU inline asm operand/parser family is source-preserved.
 * Machine constraints remain Core/Target owned. */

static bool inline_asm_identifier_is(const MinicParser *parser, const char *name) {
    size_t length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(name) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

bool minic_parser_gnu_inline_asm_starts(const MinicParser *parser) {
    return inline_asm_identifier_is(parser, "asm") || inline_asm_identifier_is(parser, "__asm") ||
           inline_asm_identifier_is(parser, "__asm__");
}

static bool current_is_gnu_goto(const MinicParser *parser) {
    return inline_asm_identifier_is(parser, "goto");
}

static bool current_is_gnu_volatile(const MinicParser *parser) {
    return parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||
           inline_asm_identifier_is(parser, "__volatile") ||
           inline_asm_identifier_is(parser, "__volatile__");
}

static bool
parse_gnu_inline_asm_operand_name(MinicParser *parser, const char **name, size_t *name_length) {
    if (parser == NULL || name == NULL || name_length == NULL) {
        return false;
    }
    *name = NULL;
    *name_length = 0U;
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return true;
    }
    if (!minic_parser_advance(parser) || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected GNU asm operand name after '['");
        return false;
    }
    *name = parser->source + parser->current.span.begin.offset;
    *name_length = minic_parser_span_length(parser->current.span);
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RBRACKET, "expected ']' after GNU asm operand name")) {
        return false;
    }
    return true;
}

static bool parse_gnu_inline_asm_output(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    const MinicExpression *operand_expression;
    MinicExpressionId operand_id;
    MinicInlineAsmOperandAccess access;
    MinicSourceSpan constraint_span;
    const char *name;
    char *constraint;
    size_t constraint_length;
    size_t name_length;

    constraint = NULL;
    constraint_length = 0U;
    name = NULL;
    name_length = 0U;
    if (!parse_gnu_inline_asm_operand_name(parser, &name, &name_length) ||
        !minic_parser_parse_string_text(
            parser, &constraint, &constraint_length, &constraint_span)) {
        free(constraint);
        return false;
    }
    if (constraint_length == 0U || (constraint[0] != '+' && constraint[0] != '=')) {
        free(constraint);
        minic_parser_error(parser, "GNU asm output constraint must begin with '+' or '='");
        return false;
    }
    access = constraint[0] == '+' ? MINIC_INLINE_ASM_OPERAND_READ_WRITE
                                  : MINIC_INLINE_ASM_OPERAND_WRITE_ONLY;
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before GNU asm output expression") ||
        !minic_parser_parse_expression_no_decay(parser, &operand_id) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm output expression")) {
        free(constraint);
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand_id);
    if (!minic_c0_inline_asm_output_value_supported(
            parser->program, parser->target_info, operand_expression,
            constraint, constraint_length)) {
        free(constraint);
        minic_parser_error(parser, "GNU asm output operand requires an lvalue");
        return false;
    }
    if (!minic_c0_program_add_inline_asm_output(parser->program,
                                                inline_asm_id,
                                                name,
                                                name_length,
                                                constraint,
                                                constraint_length,
                                                operand_id,
                                                access)) {
        free(constraint);
        minic_parser_error(parser, "cannot store GNU asm output operand");
        return false;
    }
    free(constraint);
    return true;
}

static bool parse_gnu_inline_asm_input(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    const MinicExpression *operand_expression;
    MinicExpressionId operand_id;
    MinicSourceSpan constraint_span;
    const char *name;
    char *constraint;
    size_t constraint_length;
    size_t name_length;

    constraint = NULL;
    constraint_length = 0U;
    name = NULL;
    name_length = 0U;
    if (!parse_gnu_inline_asm_operand_name(parser, &name, &name_length) ||
        !minic_parser_parse_string_text(
            parser, &constraint, &constraint_length, &constraint_span)) {
        free(constraint);
        return false;
    }
    if (constraint_length == 0U || constraint[0] == '+' || constraint[0] == '=') {
        free(constraint);
        minic_parser_error(parser, "GNU asm input constraint must describe a read-only operand");
        return false;
    }
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before GNU asm input expression") ||
        !minic_parser_parse_expression(parser, &operand_id, 0U) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm input expression")) {
        free(constraint);
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand_id);
    if (!minic_c0_inline_asm_input_value_supported(
            operand_expression, constraint, constraint_length)) {
        free(constraint);
        minic_parser_error(parser,
                           "GNU asm input operand currently requires an integer or pointer");
        return false;
    }
    if (constraint_length == 1U && constraint[0] == 'I') {
        MinicConstValue constant;
        MinicExpression folded;
        MinicSourceSpan operand_span;
        int64_t immediate_value;
        bool is_constant;

        operand_span = operand_expression->span;
        is_constant = minic_const_eval_integer(
            parser->program, parser->target_info, operand_id, &constant) &&
            minic_const_value_as_int64(
                parser->program, parser->target_info, &constant, &immediate_value);
        if (is_constant) {
            /* Even in an inline helper, a known but illegal immediate must
             * not be accepted as deferred target assembly. */
            if (!minic_target_info_inline_asm_immediate_constraint_supported(
                    parser->target_info, constraint, constraint_length, immediate_value)) {
                free(constraint);
                minic_parser_error(
                    parser, "%s",
                    minic_target_info_inline_asm_immediate_constraint_diagnostic(
                        parser->target_info));
                return false;
            }
            (void)memset(&folded, 0, sizeof(folded));
            folded.kind = MINIC_EXPRESSION_INTEGER;
            folded.span = operand_span;
            folded.type = constant.type;
            folded.value_category = MINIC_VALUE_RVALUE;
            folded.value.integer_value = immediate_value;
            if (!minic_parser_add_expression(parser, &folded, &operand_id)) {
                free(constraint);
                minic_parser_error(parser, "cannot store GNU asm immediate operand");
                return false;
            }
        } else if (!minic_target_info_inline_asm_immediate_may_defer(parser->target_info)) {
            free(constraint);
            minic_parser_error(
                parser, "%s",
                minic_target_info_inline_asm_immediate_constraint_diagnostic(
                    parser->target_info));
            return false;
        }
        /* For a GNU always-inline function, a target I-constraint may become
         * an integer constant only after caller specialization. Retain that
         * expression. Core must validate the *actual* ISA range after facts
         * and inlining; an unresolved I is never silently emitted. */
    }
    if (!minic_c0_program_add_inline_asm_input(parser->program,
                                               inline_asm_id,
                                               name,
                                               name_length,
                                               constraint,
                                               constraint_length,
                                               operand_id)) {
        free(constraint);
        minic_parser_error(parser, "cannot store GNU asm input operand");
        return false;
    }
    free(constraint);
    return true;
}

static bool parse_gnu_inline_asm_label(MinicParser *parser, MinicInlineAsmId inline_asm_id) {
    MinicSourceSpan name_span;
    MinicStatementId target_statement;
    const char *name;
    size_t name_length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected GNU asm goto label name");
        }
        return false;
    }
    name_span = parser->current.span;
    name = parser->source + name_span.begin.offset;
    name_length = minic_parser_span_length(name_span);
    target_statement = minic_parser_find_label_statement(parser, name_span);
    return minic_c0_program_add_inline_asm_label(
               parser->program, inline_asm_id, name, name_length, target_statement) &&
           minic_parser_advance(parser);
}

bool minic_parser_gnu_inline_asm_statement(MinicParser *parser) {
    MinicStatement statement;
    MinicInlineAsmId inline_asm_id;
    MinicSourcePosition begin;
    MinicSourceSpan template_span;
    char *template_text;
    size_t template_length;
    bool is_volatile;
    bool is_goto;
    bool has_inline_qualifier;
    bool has_memory_clobber;

    if (!minic_parser_gnu_inline_asm_starts(parser)) {
        return false;
    }
    begin = parser->current.span.begin;
    template_text = NULL;
    template_length = 0U;
    is_volatile = false;
    is_goto = false;
    has_inline_qualifier = false;
    has_memory_clobber = false;

    if (!minic_parser_advance(parser)) {
        return false;
    }
    for (;;) {
        /* Original x86 Linux 6.6.143 uses
         *   asm __inline volatile("...": ...);
         * GCC's asm inline modifier adjusts the optimizer's size estimate,
         * not the instruction/operand/clobber semantics. MiniC does not
         * size-estimate an asm block, so it remains parse-only. The lexer
         * already canonicalizes inline/__inline/__inline__ into KW_INLINE.
         * Leave backend qualification and machine clobbers unchanged. */
        if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
            if (has_inline_qualifier) {
                minic_parser_error(parser, "duplicate inline qualifier on GNU asm");
                return false;
            }
            has_inline_qualifier = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (current_is_gnu_volatile(parser)) {
            if (is_volatile) {
                minic_parser_error(parser, "duplicate volatile qualifier on GNU asm");
                return false;
            }
            is_volatile = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (current_is_gnu_goto(parser)) {
            if (is_goto) {
                minic_parser_error(parser, "duplicate goto qualifier on GNU asm");
                return false;
            }
            is_goto = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        break;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after GNU asm") ||
        !minic_parser_parse_string_text(parser, &template_text, &template_length, &template_span)) {
        free(template_text);
        return false;
    }
    if (!minic_c0_program_add_inline_asm(
            parser->program, template_text, template_length, is_volatile, false, &inline_asm_id) ||
        !minic_c0_program_set_inline_asm_goto(parser->program, inline_asm_id, is_goto)) {
        free(template_text);
        minic_parser_error(parser, "cannot store GNU inline assembly");
        return false;
    }
    free(template_text);

    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_output(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_input(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_COLON &&
               parser->current.kind != MINIC_TOKEN_RPAREN) {
            char *clobber;
            size_t clobber_length;
            MinicSourceSpan clobber_span;

            clobber = NULL;
            clobber_length = 0U;
            if (!minic_parser_parse_string_text(parser, &clobber, &clobber_length, &clobber_span)) {
                free(clobber);
                return false;
            }
            if (clobber_length == 6U && memcmp(clobber, "memory", 6U) == 0) {
                has_memory_clobber = true;
            } else if (!minic_target_info_inline_asm_source_clobber_supported(
                           parser->target_info, clobber, clobber_length)) {
                /* Fail closed, but identify the actual physical register or
                 * special token. Previously 771 three-ISA Linux failures all
                 * collapsed to one un-actionable identical diagnostic. No
                 * target register admission is broadened by this change. */
                minic_parser_error(parser,
                                   "unsupported GNU asm register clobber for target: %.*s",
                                   (int)clobber_length, clobber);
                free(clobber);
                return false;
            } else if (!minic_c0_program_add_inline_asm_register_clobber(
                           parser->program, inline_asm_id, clobber, clobber_length)) {
                free(clobber);
                minic_parser_error(parser, "cannot store GNU asm register clobber");
                return false;
            }
            free(clobber);
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (parser->current.kind == MINIC_TOKEN_COLON) {
        if (!is_goto) {
            minic_parser_error(parser, "GNU asm label operands require asm goto");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!parse_gnu_inline_asm_label(parser, inline_asm_id)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_COMMA) {
                break;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
    }
    if (is_goto) {
        const MinicInlineAsm *inline_asm;

        inline_asm = minic_c0_program_inline_asm(parser->program, inline_asm_id);
        if (inline_asm == NULL || inline_asm->label_count == 0U) {
            minic_parser_error(parser,
                               "GNU asm goto requires at least one label");
            return false;
        }
        /* GCC 11+ GNU asm goto MAY contain output operands. Preserve
         * their lvalue, constraints, input operands and branch labels in
         * the AST. Core's narrow zero-output static-key implementation
         * still declines this form: x86 user-access exception tables need
         * a dedicated output+label+fault-path lowering before any machine
         * bytes may be emitted. Do not discard outputs to fake success. */
    }
    if (!minic_c0_program_set_inline_asm_memory_clobber(
            parser->program, inline_asm_id, has_memory_clobber)) {
        minic_parser_error(parser, "cannot finalize GNU inline assembly metadata");
        return false;
    }

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_INLINE_ASM;
    statement.span.begin = begin;
    statement.span.end = parser->current.span.end;
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = MINIC_EXPRESSION_INVALID;
    statement.target_statement = MINIC_STATEMENT_INVALID;
    statement.inline_asm_id = inline_asm_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after GNU asm")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

