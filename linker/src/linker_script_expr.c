#include "linker_script_internal.h"

#include <stdlib.h>
#include <string.h>

static bool append_expression(MiniLdScript *script,
                              MiniLdScriptExpr expression,
                              MiniLdScriptExprId *id_out) {
    MiniLdScriptExpr *next;
    if (script->expression_count == script->expression_capacity) {
        size_t capacity = script->expression_capacity == 0U ? 64U : script->expression_capacity * 2U;
        if (capacity < script->expression_capacity ||
            capacity > SIZE_MAX / sizeof(*script->expressions)) {
            return false;
        }
        next = realloc(script->expressions, capacity * sizeof(*script->expressions));
        if (next == NULL) {
            return false;
        }
        script->expressions = next;
        script->expression_capacity = capacity;
    }
    *id_out = script->expression_count;
    script->expressions[script->expression_count++] = expression;
    return true;
}

static bool parse_primary(ScriptParser *parser, MiniLdScriptExprId *expression_out) {
    MiniLdScriptExpr expression;
    memset(&expression, 0, sizeof(expression));
    expression.left = MINILD_SCRIPT_EXPR_NONE;
    expression.right = MINILD_SCRIPT_EXPR_NONE;

    if (parser->token.kind == TOKEN_NUMBER) {
        expression.kind = MINILD_SCRIPT_EXPR_INTEGER;
        expression.integer = parser->token.number;
        if (!minild_script_parser_next(parser)) {
            return false;
        }
        return append_expression(parser->script, expression, expression_out) ||
               minild_script_parser_error(parser, "out-of-memory:expression");
    }
    if (minild_script_token_is(parser, ".")) {
        expression.kind = MINILD_SCRIPT_EXPR_DOT;
        if (!minild_script_parser_next(parser)) {
            return false;
        }
        return append_expression(parser->script, expression, expression_out) ||
               minild_script_parser_error(parser, "out-of-memory:expression");
    }
    if (parser->token.kind == TOKEN_IDENTIFIER) {
        char *name = minild_script_strdup_range(parser->token.begin, parser->token.length);
        if (name == NULL) {
            return minild_script_parser_error(parser, "out-of-memory:identifier");
        }
        if (!minild_script_parser_next(parser)) {
            free(name);
            return false;
        }
        if (parser->token.kind == TOKEN_LPAREN &&
            (strcmp(name, "ALIGN") == 0 || strcmp(name, "ADDR") == 0 ||
             strcmp(name, "ABSOLUTE") == 0)) {
            MiniLdScriptExprId operand;
            MiniLdScriptExprKind kind = strcmp(name, "ALIGN") == 0
                                            ? MINILD_SCRIPT_EXPR_ALIGN
                                        : strcmp(name, "ADDR") == 0
                                            ? MINILD_SCRIPT_EXPR_ADDR
                                            : MINILD_SCRIPT_EXPR_ABSOLUTE;
            free(name);
            if (!minild_script_parser_next(parser)) {
                return false;
            }
            if (kind == MINILD_SCRIPT_EXPR_ADDR) {
                if (parser->token.kind != TOKEN_IDENTIFIER) {
                    return minild_script_parser_error(parser, "ADDR-requires-section-name");
                }
                expression.kind = kind;
                expression.name = minild_script_strdup_range(parser->token.begin,
                                                             parser->token.length);
                if (expression.name == NULL) {
                    return minild_script_parser_error(parser, "out-of-memory:ADDR");
                }
                if (!minild_script_parser_next(parser) ||
                    !minild_script_expect(parser, TOKEN_RPAREN, "expected-')'-after-ADDR")) {
                    free(expression.name);
                    return false;
                }
                return append_expression(parser->script, expression, expression_out) ||
                       minild_script_parser_error(parser, "out-of-memory:expression");
            }
            if (!minild_script_parse_expression(parser, &operand) ||
                !minild_script_expect(parser, TOKEN_RPAREN, "expected-')'-after-function")) {
                return false;
            }
            expression.kind = kind;
            expression.left = operand;
            return append_expression(parser->script, expression, expression_out) ||
                   minild_script_parser_error(parser, "out-of-memory:expression");
        }
        expression.kind = MINILD_SCRIPT_EXPR_SYMBOL;
        expression.name = name;
        return append_expression(parser->script, expression, expression_out) ||
               minild_script_parser_error(parser, "out-of-memory:expression");
    }
    if (minild_script_consume(parser, TOKEN_LPAREN)) {
        if (!minild_script_parse_expression(parser, expression_out) ||
            !minild_script_expect(parser, TOKEN_RPAREN, "expected-')'")) {
            return false;
        }
        return true;
    }
    return minild_script_parser_error(parser, "expected-expression");
}

static bool parse_unary(ScriptParser *parser, MiniLdScriptExprId *expression_out) {
    if (parser->token.kind == TOKEN_PLUS) {
        return minild_script_parser_next(parser) && parse_unary(parser, expression_out);
    }
    if (parser->token.kind == TOKEN_MINUS) {
        MiniLdScriptExpr expression;
        MiniLdScriptExprId operand;
        if (!minild_script_parser_next(parser) || !parse_unary(parser, &operand)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = MINILD_SCRIPT_EXPR_NEGATE;
        expression.left = operand;
        expression.right = MINILD_SCRIPT_EXPR_NONE;
        return append_expression(parser->script, expression, expression_out) ||
               minild_script_parser_error(parser, "out-of-memory:expression");
    }
    return parse_primary(parser, expression_out);
}

static bool parse_multiplicative(ScriptParser *parser,
                                 MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!parse_unary(parser, &left)) {
        return false;
    }
    for (;;) {
        ScriptTokenKind token = parser->token.kind;
        MiniLdScriptExprKind kind;
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        if (token != TOKEN_STAR && token != TOKEN_SLASH) {
            break;
        }
        kind = token == TOKEN_STAR ? MINILD_SCRIPT_EXPR_MULTIPLY
                                   : MINILD_SCRIPT_EXPR_DIVIDE;
        if (!minild_script_parser_next(parser) || !parse_unary(parser, &right)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = kind;
        expression.left = left;
        expression.right = right;
        if (!append_expression(parser->script, expression, &left)) {
            return minild_script_parser_error(parser, "out-of-memory:expression");
        }
    }
    *expression_out = left;
    return true;
}

static bool parse_additive(ScriptParser *parser, MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!parse_multiplicative(parser, &left)) {
        return false;
    }
    while (parser->token.kind == TOKEN_PLUS || parser->token.kind == TOKEN_MINUS) {
        ScriptTokenKind token = parser->token.kind;
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        if (!minild_script_parser_next(parser) ||
            !parse_multiplicative(parser, &right)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = token == TOKEN_PLUS ? MINILD_SCRIPT_EXPR_ADD
                                              : MINILD_SCRIPT_EXPR_SUBTRACT;
        expression.left = left;
        expression.right = right;
        if (!append_expression(parser->script, expression, &left)) {
            return minild_script_parser_error(parser, "out-of-memory:expression");
        }
    }
    *expression_out = left;
    return true;
}

bool minild_script_parse_expression(ScriptParser *parser,
                                    MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!parse_additive(parser, &left)) {
        return false;
    }
    while (parser->token.kind == TOKEN_SHIFT_LEFT) {
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        if (!minild_script_parser_next(parser) ||
            !parse_additive(parser, &right)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = MINILD_SCRIPT_EXPR_SHIFT_LEFT;
        expression.left = left;
        expression.right = right;
        if (!append_expression(parser->script, expression, &left)) {
            return minild_script_parser_error(parser, "out-of-memory:expression");
        }
    }
    *expression_out = left;
    return true;
}

static bool evaluate_expression(const MiniLdScript *script,
                                MiniLdScriptExprId id,
                                const MiniLdScriptEvalContext *context,
                                uint64_t *value_out,
                                FILE *diagnostics,
                                size_t depth) {
    const MiniLdScriptExpr *expression;
    uint64_t left;
    uint64_t right;

    if (depth > 256U || id >= script->expression_count) {
        fprintf(diagnostics, "minic-ld: linker-script-expression-invalid\n");
        return false;
    }
    expression = &script->expressions[id];
    switch (expression->kind) {
    case MINILD_SCRIPT_EXPR_INTEGER:
        *value_out = expression->integer;
        return true;
    case MINILD_SCRIPT_EXPR_DOT:
        *value_out = context->dot;
        return true;
    case MINILD_SCRIPT_EXPR_SYMBOL:
        if (context->resolve_symbol != NULL &&
            context->resolve_symbol(context->user, expression->name, value_out)) {
            return true;
        }
        fprintf(diagnostics,
                "minic-ld: linker-script-undefined-symbol:%s\n",
                expression->name);
        return false;
    case MINILD_SCRIPT_EXPR_ADDR:
        if (context->resolve_section != NULL &&
            context->resolve_section(context->user, expression->name, value_out)) {
            return true;
        }
        fprintf(diagnostics,
                "minic-ld: linker-script-unknown-section:%s\n",
                expression->name);
        return false;
    case MINILD_SCRIPT_EXPR_NEGATE:
        if (!evaluate_expression(script,
                                 expression->left,
                                 context,
                                 &left,
                                 diagnostics,
                                 depth + 1U)) {
            return false;
        }
        *value_out = UINT64_C(0) - left;
        return true;
    case MINILD_SCRIPT_EXPR_ALIGN:
        if (!evaluate_expression(script,
                                 expression->left,
                                 context,
                                 &right,
                                 diagnostics,
                                 depth + 1U) ||
            right == 0U || (right & (right - 1U)) != 0U) {
            fprintf(diagnostics, "minic-ld: linker-script-invalid-alignment\n");
            return false;
        }
        *value_out = (context->dot + right - 1U) & ~(right - 1U);
        return true;
    case MINILD_SCRIPT_EXPR_ABSOLUTE:
        return evaluate_expression(script,
                                   expression->left,
                                   context,
                                   value_out,
                                   diagnostics,
                                   depth + 1U);
    case MINILD_SCRIPT_EXPR_ADD:
    case MINILD_SCRIPT_EXPR_SUBTRACT:
    case MINILD_SCRIPT_EXPR_MULTIPLY:
    case MINILD_SCRIPT_EXPR_DIVIDE:
    case MINILD_SCRIPT_EXPR_SHIFT_LEFT:
        if (!evaluate_expression(script,
                                 expression->left,
                                 context,
                                 &left,
                                 diagnostics,
                                 depth + 1U) ||
            !evaluate_expression(script,
                                 expression->right,
                                 context,
                                 &right,
                                 diagnostics,
                                 depth + 1U)) {
            return false;
        }
        if (expression->kind == MINILD_SCRIPT_EXPR_ADD) {
            *value_out = left + right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_SUBTRACT) {
            *value_out = left - right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_MULTIPLY) {
            *value_out = left * right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_DIVIDE) {
            if (right == 0U) {
                fprintf(diagnostics, "minic-ld: linker-script-division-by-zero\n");
                return false;
            }
            *value_out = left / right;
        } else {
            if (right >= 64U) {
                fprintf(diagnostics, "minic-ld: linker-script-invalid-shift\n");
                return false;
            }
            *value_out = left << right;
        }
        return true;
    }
    fprintf(diagnostics, "minic-ld: linker-script-expression-unsupported\n");
    return false;
}

bool minild_script_evaluate(const MiniLdScript *script,
                            MiniLdScriptExprId expression,
                            const MiniLdScriptEvalContext *context,
                            uint64_t *value_out,
                            FILE *diagnostics) {
    if (script == NULL || context == NULL || value_out == NULL ||
        diagnostics == NULL) {
        return false;
    }
    return evaluate_expression(script,
                               expression,
                               context,
                               value_out,
                               diagnostics,
                               0U);
}
