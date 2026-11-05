#include "linker_script_internal.h"

#include <stdlib.h>
#include <string.h>

bool minild_script_append_expression(MiniLdScript *script,
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

static bool append_expression(MiniLdScript *script,
                              MiniLdScriptExpr expression,
                              MiniLdScriptExprId *id_out) {
    return minild_script_append_expression(script, expression, id_out);
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
    if (minild_script_token_is(parser, "SIZEOF_HEADERS")) {
        /* GNU linker expression, not a user-defined ELF symbol. The caller
         * must explicitly supply the actual ELF + PHDR size or evaluation
         * fails closed; never guess based on the number of sections. */
        expression.kind = MINILD_SCRIPT_EXPR_SIZEOF_HEADERS;
        if (!minild_script_parser_next(parser)) {
            return false;
        }
        return append_expression(parser->script, expression, expression_out) ||
               minild_script_parser_error(parser, "out-of-memory:SIZEOF_HEADERS");
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
             strcmp(name, "SIZEOF") == 0 || strcmp(name, "ASSERT") == 0 ||
             strcmp(name, "ABSOLUTE") == 0)) {
            MiniLdScriptExprId operand;
            MiniLdScriptExprKind kind = strcmp(name, "ALIGN") == 0
                                            ? MINILD_SCRIPT_EXPR_ALIGN
                                        : strcmp(name, "ADDR") == 0
                                            ? MINILD_SCRIPT_EXPR_ADDR
                                        : strcmp(name, "SIZEOF") == 0
                                            ? MINILD_SCRIPT_EXPR_SIZEOF
                                        : strcmp(name, "ASSERT") == 0
                                            ? MINILD_SCRIPT_EXPR_ASSERT
                                            : MINILD_SCRIPT_EXPR_ABSOLUTE;
            free(name);
            if (!minild_script_parser_next(parser)) {
                return false;
            }
            if (kind == MINILD_SCRIPT_EXPR_ADDR ||
                kind == MINILD_SCRIPT_EXPR_SIZEOF) {
                if (parser->token.kind != TOKEN_IDENTIFIER) {
                    return minild_script_parser_error(parser, "section-expression-requires-section-name");
                }
                expression.kind = kind;
                expression.name = minild_script_strdup_range(parser->token.begin,
                                                             parser->token.length);
                if (expression.name == NULL) {
                    return minild_script_parser_error(parser, "out-of-memory:section-expression");
                }
                if (!minild_script_parser_next(parser) ||
                    !minild_script_expect(parser, TOKEN_RPAREN, "expected-')'-after-section-expression")) {
                    free(expression.name);
                    return false;
                }
                return append_expression(parser->script, expression, expression_out) ||
                       minild_script_parser_error(parser, "out-of-memory:expression");
            }
            if (!minild_script_parse_expression(parser, &operand)) {
                return false;
            }
            expression.kind = kind;
            expression.left = operand;
            if (kind == MINILD_SCRIPT_EXPR_ALIGN &&
                parser->token.kind == TOKEN_COMMA) {
                if (!minild_script_parser_next(parser) ||
                    !minild_script_parse_expression(parser, &expression.right)) {
                    return false;
                }
            }
            if (kind == MINILD_SCRIPT_EXPR_ASSERT) {
                if (!minild_script_expect(parser, TOKEN_COMMA,
                                          "expected-','-after-ASSERT-expression") ||
                    parser->token.kind != TOKEN_STRING) {
                    return minild_script_parser_error(parser,
                                                      "expected-ASSERT-message");
                }
                expression.name = minild_script_strdup_range(
                    parser->token.begin, parser->token.length);
                if (expression.name == NULL ||
                    !minild_script_parser_next(parser)) {
                    free(expression.name);
                    return false;
                }
            }
            if (!minild_script_expect(parser, TOKEN_RPAREN,
                                      "expected-')'-after-function")) {
                free(expression.name);
                return false;
            }
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
    if (parser->token.kind == TOKEN_MINUS ||
        parser->token.kind == TOKEN_TILDE ||
        parser->token.kind == TOKEN_LOGICAL_NOT) {
        ScriptTokenKind token = parser->token.kind;
        MiniLdScriptExpr expression;
        MiniLdScriptExprId operand;
        if (!minild_script_parser_next(parser) || !parse_unary(parser, &operand)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = token == TOKEN_MINUS ? MINILD_SCRIPT_EXPR_NEGATE :
                          token == TOKEN_TILDE ? MINILD_SCRIPT_EXPR_BITWISE_NOT :
                                                 MINILD_SCRIPT_EXPR_LOGICAL_NOT;
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
        if (token != TOKEN_STAR && token != TOKEN_SLASH &&
            token != TOKEN_PERCENT) {
            break;
        }
        kind = token == TOKEN_STAR ? MINILD_SCRIPT_EXPR_MULTIPLY :
               token == TOKEN_SLASH ? MINILD_SCRIPT_EXPR_DIVIDE :
                                       MINILD_SCRIPT_EXPR_MODULO;
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

static bool parse_shift(ScriptParser *parser,
                        MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!parse_additive(parser, &left)) {
        return false;
    }
    while (parser->token.kind == TOKEN_SHIFT_LEFT ||
           parser->token.kind == TOKEN_SHIFT_RIGHT) {
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        ScriptTokenKind token = parser->token.kind;
        if (!minild_script_parser_next(parser) ||
            !parse_additive(parser, &right)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = token == TOKEN_SHIFT_LEFT
                              ? MINILD_SCRIPT_EXPR_SHIFT_LEFT
                              : MINILD_SCRIPT_EXPR_SHIFT_RIGHT;
        expression.left = left;
        expression.right = right;
        if (!append_expression(parser->script, expression, &left)) {
            return minild_script_parser_error(parser, "out-of-memory:expression");
        }
    }
    *expression_out = left;
    return true;
}

/* GNU ld expression precedence: additive > shift > bitwise AND > XOR > OR.
 * Keep the bitwise levels a single recursive grammar shared by all targets,
 * including LoongArch's kernel_entry & PHYSADDR_MASK Kbuild expression. */
typedef bool (*MiniLdParseExprLevel)(ScriptParser *, MiniLdScriptExprId *);

static bool parse_bitwise_level(ScriptParser *parser,
                                MiniLdParseExprLevel tighter,
                                ScriptTokenKind token,
                                MiniLdScriptExprKind kind,
                                MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!tighter(parser, &left)) {
        return false;
    }
    while (parser->token.kind == token) {
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        if (!minild_script_parser_next(parser) || !tighter(parser, &right)) {
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

static bool parse_bitwise_and(ScriptParser *parser,
                              MiniLdScriptExprId *expression_out) {
    return parse_bitwise_level(parser, parse_shift, TOKEN_AMP,
                               MINILD_SCRIPT_EXPR_BITWISE_AND, expression_out);
}

static bool parse_bitwise_xor(ScriptParser *parser,
                              MiniLdScriptExprId *expression_out) {
    return parse_bitwise_level(parser, parse_bitwise_and, TOKEN_CARET,
                               MINILD_SCRIPT_EXPR_BITWISE_XOR, expression_out);
}

static bool parse_bitwise_or(ScriptParser *parser,
                             MiniLdScriptExprId *expression_out) {
    return parse_bitwise_level(parser, parse_bitwise_xor, TOKEN_PIPE,
                               MINILD_SCRIPT_EXPR_BITWISE_OR, expression_out);
}

static bool parse_comparison(ScriptParser *parser,
                             MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!parse_bitwise_or(parser, &left)) {
        return false;
    }
    while (parser->token.kind == TOKEN_LESS ||
           parser->token.kind == TOKEN_LESS_EQUAL ||
           parser->token.kind == TOKEN_GREATER ||
           parser->token.kind == TOKEN_GREATER_EQUAL) {
        ScriptTokenKind token = parser->token.kind;
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        if (!minild_script_parser_next(parser) ||
            !parse_bitwise_or(parser, &right)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = token == TOKEN_LESS ? MINILD_SCRIPT_EXPR_LESS :
                          token == TOKEN_LESS_EQUAL ? MINILD_SCRIPT_EXPR_LESS_EQUAL :
                          token == TOKEN_GREATER ? MINILD_SCRIPT_EXPR_GREATER :
                          MINILD_SCRIPT_EXPR_GREATER_EQUAL;
        expression.left = left;
        expression.right = right;
        if (!append_expression(parser->script, expression, &left)) {
            return minild_script_parser_error(parser, "out-of-memory:expression");
        }
    }
    *expression_out = left;
    return true;
}

static bool parse_equality(ScriptParser *parser,
                           MiniLdScriptExprId *expression_out) {
    MiniLdScriptExprId left;
    if (!parse_comparison(parser, &left)) {
        return false;
    }
    while (parser->token.kind == TOKEN_EQUAL_EQUAL ||
           parser->token.kind == TOKEN_NOT_EQUAL) {
        ScriptTokenKind token = parser->token.kind;
        MiniLdScriptExpr expression;
        MiniLdScriptExprId right;
        if (!minild_script_parser_next(parser) ||
            !parse_comparison(parser, &right)) {
            return false;
        }
        memset(&expression, 0, sizeof(expression));
        expression.kind = token == TOKEN_EQUAL_EQUAL
                              ? MINILD_SCRIPT_EXPR_EQUAL
                              : MINILD_SCRIPT_EXPR_NOT_EQUAL;
        expression.left = left;
        expression.right = right;
        if (!append_expression(parser->script, expression, &left)) {
            return minild_script_parser_error(parser,
                                              "out-of-memory:expression");
        }
    }
    *expression_out = left;
    return true;
}

static bool parse_logical_and(ScriptParser *parser,
                              MiniLdScriptExprId *expression_out) {
    return parse_bitwise_level(parser, parse_equality, TOKEN_LOGICAL_AND,
                               MINILD_SCRIPT_EXPR_LOGICAL_AND, expression_out);
}

bool minild_script_parse_expression(ScriptParser *parser,
                                    MiniLdScriptExprId *expression_out) {
    return parse_bitwise_level(parser, parse_logical_and, TOKEN_LOGICAL_OR,
                               MINILD_SCRIPT_EXPR_LOGICAL_OR, expression_out);
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
    case MINILD_SCRIPT_EXPR_SIZEOF_HEADERS:
        if (!context->have_headers_size) {
            fprintf(diagnostics,
                    "minic-ld: linker-script-SIZEOF_HEADERS-unknown\n");
            return false;
        }
        *value_out = context->headers_size;
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
    case MINILD_SCRIPT_EXPR_SIZEOF:
        if (expression->kind == MINILD_SCRIPT_EXPR_SIZEOF) {
            if (context->resolve_section_size != NULL &&
                context->resolve_section_size(context->user, expression->name,
                                              value_out)) {
                return true;
            }
            fprintf(diagnostics,
                    "minic-ld: linker-script-unknown-section-size:%s\n",
                    expression->name);
            return false;
        }
        if (context->resolve_section != NULL &&
            context->resolve_section(context->user, expression->name, value_out)) {
            return true;
        }
        fprintf(diagnostics,
                "minic-ld: linker-script-unknown-section:%s\n",
                expression->name);
        return false;
    case MINILD_SCRIPT_EXPR_NEGATE:
    case MINILD_SCRIPT_EXPR_BITWISE_NOT:
    case MINILD_SCRIPT_EXPR_LOGICAL_NOT:
        if (!evaluate_expression(script,
                                 expression->left,
                                 context,
                                 &left,
                                 diagnostics,
                                 depth + 1U)) {
            return false;
        }
        *value_out = expression->kind == MINILD_SCRIPT_EXPR_NEGATE
                         ? UINT64_C(0) - left :
                     expression->kind == MINILD_SCRIPT_EXPR_LOGICAL_NOT
                         ? (left == 0U ? 1U : 0U) : ~left;
        return true;
    case MINILD_SCRIPT_EXPR_ALIGN:
        if (!evaluate_expression(script,
                                 expression->left,
                                 context,
                                 &right,
                                 diagnostics,
                                 depth + 1U)) {
            return false;
        }
        if (expression->right != MINILD_SCRIPT_EXPR_NONE) {
            left = right;
            if (!evaluate_expression(script, expression->right, context,
                                     &right, diagnostics, depth + 1U)) {
                return false;
            }
        } else {
            left = context->dot;
        }
        if (right == 0U) {
            *value_out = left;
            return true;
        }
        if ((right & (right - 1U)) != 0U) {
            fprintf(diagnostics, "minic-ld: linker-script-invalid-alignment\n");
            return false;
        }
        *value_out = (left + right - 1U) & ~(right - 1U);
        return true;
    case MINILD_SCRIPT_EXPR_ASSERT:
        if (!evaluate_expression(script, expression->left, context, &left,
                                 diagnostics, depth + 1U)) {
            return false;
        }
        if (left == 0U) {
            fprintf(diagnostics, "minic-ld: linker-script-assertion-failed:%s\n",
                    expression->name != NULL ? expression->name : "");
            return false;
        }
        *value_out = 1U;
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
    case MINILD_SCRIPT_EXPR_MODULO:
    case MINILD_SCRIPT_EXPR_SHIFT_LEFT:
    case MINILD_SCRIPT_EXPR_SHIFT_RIGHT:
    case MINILD_SCRIPT_EXPR_BITWISE_AND:
    case MINILD_SCRIPT_EXPR_BITWISE_XOR:
    case MINILD_SCRIPT_EXPR_BITWISE_OR:
    case MINILD_SCRIPT_EXPR_LESS:
    case MINILD_SCRIPT_EXPR_LESS_EQUAL:
    case MINILD_SCRIPT_EXPR_GREATER:
    case MINILD_SCRIPT_EXPR_GREATER_EQUAL:
    case MINILD_SCRIPT_EXPR_EQUAL:
    case MINILD_SCRIPT_EXPR_NOT_EQUAL:
    case MINILD_SCRIPT_EXPR_LOGICAL_AND:
    case MINILD_SCRIPT_EXPR_LOGICAL_OR:
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
        } else if (expression->kind == MINILD_SCRIPT_EXPR_DIVIDE ||
                   expression->kind == MINILD_SCRIPT_EXPR_MODULO) {
            if (right == 0U) {
                fprintf(diagnostics, "minic-ld: linker-script-division-by-zero\n");
                return false;
            }
            *value_out = expression->kind == MINILD_SCRIPT_EXPR_DIVIDE
                             ? left / right : left % right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_SHIFT_LEFT ||
                   expression->kind == MINILD_SCRIPT_EXPR_SHIFT_RIGHT) {
            if (right >= 64U) {
                fprintf(diagnostics, "minic-ld: linker-script-invalid-shift\n");
                return false;
            }
            *value_out = expression->kind == MINILD_SCRIPT_EXPR_SHIFT_LEFT
                             ? left << right : left >> right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_BITWISE_AND) {
            *value_out = left & right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_BITWISE_XOR) {
            *value_out = left ^ right;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_LESS) {
            *value_out = left < right ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_LESS_EQUAL) {
            *value_out = left <= right ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_GREATER) {
            *value_out = left > right ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_GREATER_EQUAL) {
            *value_out = left >= right ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_EQUAL) {
            *value_out = left == right ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_NOT_EQUAL) {
            *value_out = left != right ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_LOGICAL_AND) {
            *value_out = left != 0U && right != 0U ? 1U : 0U;
        } else if (expression->kind == MINILD_SCRIPT_EXPR_LOGICAL_OR) {
            *value_out = left != 0U || right != 0U ? 1U : 0U;
        } else {
            *value_out = left | right;
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
