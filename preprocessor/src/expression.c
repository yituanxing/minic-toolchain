#include "minipp_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiniPpInt {
    uint64_t bits;
    bool is_unsigned;
} MiniPpInt;

typedef struct MiniPpExprParser {
    MiniPpState *state;
    const char *cursor;
} MiniPpExprParser;

static void minipp_expr_skip_space(MiniPpExprParser *parser) {
    while (isspace((unsigned char)*parser->cursor) != 0 ||
           *parser->cursor == '\x13') {
        ++parser->cursor;
    }
}

static bool minipp_expr_identifier_start(char value) {
    return value == '_' || isalpha((unsigned char)value) != 0;
}

static bool minipp_expr_identifier_continue(char value) {
    return value == '_' || isalnum((unsigned char)value) != 0;
}

static bool minipp_expr_macro_defined(const MiniPpState *state,
                                      const char *name,
                                      size_t size) {
    size_t index;

    for (index = 0U; index < state->macro_count; ++index) {
        const MiniPpMacro *macro = &state->macros[index];
        if (strlen(macro->name) == size &&
            memcmp(macro->name, name, size) == 0) {
            return true;
        }
    }
    return false;
}

static MiniPpInt minipp_expr_bool(bool value) {
    MiniPpInt result;
    result.bits = value ? 1U : 0U;
    result.is_unsigned = false;
    return result;
}

static bool minipp_expr_truth(MiniPpInt value) {
    return value.bits != 0U;
}

static int64_t minipp_expr_signed(MiniPpInt value) {
    return (int64_t)value.bits;
}

static bool minipp_expr_is_identifier_boundary(char value) {
    return !minipp_expr_identifier_continue(value);
}

static bool minipp_replace_defined(MiniPpState *state,
                                   const char *expression,
                                   MiniPpString *output) {
    size_t index = 0U;

    minipp_string_init(output);
    while (expression[index] != '\0') {
        if (minipp_expr_identifier_start(expression[index])) {
            size_t start = index;
            size_t size;

            ++index;
            while (minipp_expr_identifier_continue(expression[index])) {
                ++index;
            }
            size = index - start;

            if (size == 7U &&
                memcmp(expression + start, "defined", 7U) == 0 &&
                minipp_expr_is_identifier_boundary(expression[index])) {
                size_t cursor = index;
                size_t name_start;
                size_t name_size;
                bool parenthesized = false;
                bool present;

                while (expression[cursor] == ' ' ||
                       expression[cursor] == '\t' ||
                       expression[cursor] == '\v' ||
                       expression[cursor] == '\f') {
                    ++cursor;
                }
                if (expression[cursor] == '(') {
                    parenthesized = true;
                    ++cursor;
                    while (expression[cursor] == ' ' ||
                           expression[cursor] == '\t' ||
                           expression[cursor] == '\v' ||
                           expression[cursor] == '\f') {
                        ++cursor;
                    }
                }
                name_start = cursor;
                if (!minipp_expr_identifier_start(expression[cursor])) {
                    fprintf(state->diagnostics,
                            "minic-cpp: invalid-defined-operand:%s",
                            expression);
                    minipp_string_destroy(output);
                    return false;
                }
                ++cursor;
                while (minipp_expr_identifier_continue(expression[cursor])) {
                    ++cursor;
                }
                name_size = cursor - name_start;
                while (expression[cursor] == ' ' ||
                       expression[cursor] == '\t' ||
                       expression[cursor] == '\v' ||
                       expression[cursor] == '\f') {
                    ++cursor;
                }
                if (parenthesized) {
                    if (expression[cursor] != ')') {
                        fprintf(state->diagnostics,
                                "minic-cpp: invalid-defined-operand:%s",
                                expression);
                        minipp_string_destroy(output);
                        return false;
                    }
                    ++cursor;
                }

                present = minipp_expr_macro_defined(state,
                                                    expression + name_start,
                                                    name_size);
                if (!minipp_string_append_char(output, present ? '1' : '0')) {
                    fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                    minipp_string_destroy(output);
                    return false;
                }
                index = cursor;
                continue;
            }

            if (!minipp_string_append_n(output,
                                        expression + start,
                                        size)) {
                fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
                minipp_string_destroy(output);
                return false;
            }
            continue;
        }

        if (!minipp_string_append_char(output, expression[index])) {
            fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
            minipp_string_destroy(output);
            return false;
        }
        ++index;
    }

    if (!minipp_string_append_char(output, '\0')) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        minipp_string_destroy(output);
        return false;
    }
    --output->size;
    return true;
}

static bool minipp_expr_parse_integer(MiniPpExprParser *parser,
                                      MiniPpInt *value) {
    const char *start = parser->cursor;
    char *end = NULL;
    unsigned long long parsed;
    bool unsigned_suffix = false;
    const char *suffix;

    errno = 0;
    parsed = strtoull(start, &end, 0);
    if (end == start || errno == ERANGE) {
        return false;
    }

    suffix = end;
    while (*suffix == 'u' || *suffix == 'U' ||
           *suffix == 'l' || *suffix == 'L') {
        if (*suffix == 'u' || *suffix == 'U') {
            unsigned_suffix = true;
        }
        ++suffix;
    }
    if (minipp_expr_identifier_continue(*suffix)) {
        return false;
    }

    value->bits = (uint64_t)parsed;
    value->is_unsigned = unsigned_suffix ||
                         (uint64_t)parsed > (uint64_t)INT64_MAX;
    parser->cursor = suffix;
    return true;
}

static bool minipp_expr_parse_character(MiniPpExprParser *parser,
                                        MiniPpInt *value) {
    const char *cursor = parser->cursor;
    uint64_t result = 0U;
    size_t count = 0U;

    if ((cursor[0] == 'L' || cursor[0] == 'u' || cursor[0] == 'U') &&
        cursor[1] == '\'') {
        ++cursor;
    }
    if (*cursor != '\'') {
        return false;
    }
    ++cursor;

    while (*cursor != '\0' && *cursor != '\'') {
        unsigned int ch;

        if (*cursor == '\\') {
            ++cursor;
            switch (*cursor) {
            case 'n': ch = '\n'; ++cursor; break;
            case 'r': ch = '\r'; ++cursor; break;
            case 't': ch = '\t'; ++cursor; break;
            case 'v': ch = '\v'; ++cursor; break;
            case 'f': ch = '\f'; ++cursor; break;
            case 'a': ch = '\a'; ++cursor; break;
            case 'b': ch = '\b'; ++cursor; break;
            case '\\': ch = '\\'; ++cursor; break;
            case '\'': ch = '\''; ++cursor; break;
            case '"': ch = '"'; ++cursor; break;
            case 'x': {
                unsigned int hex = 0U;
                size_t digits = 0U;
                ++cursor;
                while (isxdigit((unsigned char)*cursor) != 0) {
                    unsigned int digit;
                    if (*cursor >= '0' && *cursor <= '9') {
                        digit = (unsigned int)(*cursor - '0');
                    } else if (*cursor >= 'a' && *cursor <= 'f') {
                        digit = 10U + (unsigned int)(*cursor - 'a');
                    } else {
                        digit = 10U + (unsigned int)(*cursor - 'A');
                    }
                    hex = (hex << 4U) | digit;
                    ++cursor;
                    ++digits;
                }
                if (digits == 0U) {
                    return false;
                }
                ch = hex & 0xffU;
                break;
            }
            default:
                if (*cursor >= '0' && *cursor <= '7') {
                    unsigned int octal = 0U;
                    size_t digits = 0U;
                    while (digits < 3U &&
                           *cursor >= '0' && *cursor <= '7') {
                        octal = (octal << 3U) |
                                (unsigned int)(*cursor - '0');
                        ++cursor;
                        ++digits;
                    }
                    ch = octal & 0xffU;
                } else {
                    ch = (unsigned char)*cursor;
                    if (*cursor != '\0') {
                        ++cursor;
                    }
                }
                break;
            }
        } else {
            ch = (unsigned char)*cursor;
            ++cursor;
        }

        result = (result << 8U) | (uint64_t)(ch & 0xffU);
        ++count;
    }

    if (*cursor != '\'' || count == 0U) {
        return false;
    }
    ++cursor;
    value->bits = result;
    value->is_unsigned = false;
    parser->cursor = cursor;
    return true;
}

static bool minipp_expr_parse_conditional(MiniPpExprParser *parser,
                                          MiniPpInt *value);

static bool minipp_expr_parse_primary(MiniPpExprParser *parser,
                                      MiniPpInt *value) {
    minipp_expr_skip_space(parser);

    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!minipp_expr_parse_conditional(parser, value)) {
            return false;
        }
        minipp_expr_skip_space(parser);
        if (*parser->cursor != ')') {
            return false;
        }
        ++parser->cursor;
        return true;
    }

    if (*parser->cursor == '\'' ||
        ((parser->cursor[0] == 'L' || parser->cursor[0] == 'u' ||
          parser->cursor[0] == 'U') &&
         parser->cursor[1] == '\'')) {
        return minipp_expr_parse_character(parser, value);
    }

    if (isdigit((unsigned char)*parser->cursor) != 0) {
        return minipp_expr_parse_integer(parser, value);
    }

    if (minipp_expr_identifier_start(*parser->cursor)) {
        ++parser->cursor;
        while (minipp_expr_identifier_continue(*parser->cursor)) {
            ++parser->cursor;
        }
        value->bits = 0U;
        value->is_unsigned = false;
        return true;
    }

    return false;
}

static bool minipp_expr_parse_unary(MiniPpExprParser *parser,
                                    MiniPpInt *value) {
    minipp_expr_skip_space(parser);

    if (*parser->cursor == '+') {
        ++parser->cursor;
        return minipp_expr_parse_unary(parser, value);
    }
    if (*parser->cursor == '-') {
        ++parser->cursor;
        if (!minipp_expr_parse_unary(parser, value)) {
            return false;
        }
        value->bits = 0U - value->bits;
        return true;
    }
    if (*parser->cursor == '!') {
        ++parser->cursor;
        if (!minipp_expr_parse_unary(parser, value)) {
            return false;
        }
        *value = minipp_expr_bool(!minipp_expr_truth(*value));
        return true;
    }
    if (*parser->cursor == '~') {
        ++parser->cursor;
        if (!minipp_expr_parse_unary(parser, value)) {
            return false;
        }
        value->bits = ~value->bits;
        return true;
    }

    return minipp_expr_parse_primary(parser, value);
}

static bool minipp_expr_parse_mul(MiniPpExprParser *parser,
                                  MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_unary(parser, value)) {
        return false;
    }
    for (;;) {
        char op;

        minipp_expr_skip_space(parser);
        op = *parser->cursor;
        if (op != '*' && op != '/' && op != '%') {
            return true;
        }
        ++parser->cursor;
        if (!minipp_expr_parse_unary(parser, &rhs)) {
            return false;
        }

        if (op == '*') {
            value->bits *= rhs.bits;
        } else {
            if (rhs.bits == 0U) {
                fprintf(parser->state->diagnostics,
                        "minic-cpp: division-by-zero-in-if\n");
                return false;
            }
            if (value->is_unsigned || rhs.is_unsigned) {
                if (op == '/') {
                    value->bits /= rhs.bits;
                } else {
                    value->bits %= rhs.bits;
                }
                value->is_unsigned = true;
            } else {
                int64_t lhs_signed = minipp_expr_signed(*value);
                int64_t rhs_signed = minipp_expr_signed(rhs);
                if (lhs_signed == INT64_MIN && rhs_signed == -1) {
                    value->bits = (uint64_t)INT64_MIN;
                } else if (op == '/') {
                    value->bits =
                        (uint64_t)(lhs_signed / rhs_signed);
                } else {
                    value->bits =
                        (uint64_t)(lhs_signed % rhs_signed);
                }
            }
        }
        value->is_unsigned = value->is_unsigned || rhs.is_unsigned;
    }
}

static bool minipp_expr_parse_add(MiniPpExprParser *parser,
                                  MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_mul(parser, value)) {
        return false;
    }
    for (;;) {
        char op;

        minipp_expr_skip_space(parser);
        op = *parser->cursor;
        if (op != '+' && op != '-') {
            return true;
        }
        ++parser->cursor;
        if (!minipp_expr_parse_mul(parser, &rhs)) {
            return false;
        }
        if (op == '+') {
            value->bits += rhs.bits;
        } else {
            value->bits -= rhs.bits;
        }
        value->is_unsigned = value->is_unsigned || rhs.is_unsigned;
    }
}

static bool minipp_expr_parse_shift(MiniPpExprParser *parser,
                                    MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_add(parser, value)) {
        return false;
    }
    for (;;) {
        bool left;

        minipp_expr_skip_space(parser);
        if (strncmp(parser->cursor, "<<", 2U) == 0) {
            left = true;
        } else if (strncmp(parser->cursor, ">>", 2U) == 0) {
            left = false;
        } else {
            return true;
        }
        parser->cursor += 2;
        if (!minipp_expr_parse_add(parser, &rhs)) {
            return false;
        }
        if (rhs.bits >= 64U) {
            value->bits = 0U;
        } else if (left) {
            value->bits <<= (unsigned int)rhs.bits;
        } else if (value->is_unsigned) {
            value->bits >>= (unsigned int)rhs.bits;
        } else {
            value->bits =
                (uint64_t)(minipp_expr_signed(*value) >>
                           (unsigned int)rhs.bits);
        }
    }
}

static bool minipp_expr_compare(MiniPpInt lhs,
                                MiniPpInt rhs,
                                int relation) {
    if (lhs.is_unsigned || rhs.is_unsigned) {
        switch (relation) {
        case -2: return lhs.bits < rhs.bits;
        case -1: return lhs.bits <= rhs.bits;
        case 1: return lhs.bits >= rhs.bits;
        case 2: return lhs.bits > rhs.bits;
        default: return false;
        }
    }

    switch (relation) {
    case -2: return minipp_expr_signed(lhs) < minipp_expr_signed(rhs);
    case -1: return minipp_expr_signed(lhs) <= minipp_expr_signed(rhs);
    case 1: return minipp_expr_signed(lhs) >= minipp_expr_signed(rhs);
    case 2: return minipp_expr_signed(lhs) > minipp_expr_signed(rhs);
    default: return false;
    }
}

static bool minipp_expr_parse_relational(MiniPpExprParser *parser,
                                         MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_shift(parser, value)) {
        return false;
    }
    for (;;) {
        int relation = 0;

        minipp_expr_skip_space(parser);
        if (strncmp(parser->cursor, "<=", 2U) == 0) {
            relation = -1;
            parser->cursor += 2;
        } else if (strncmp(parser->cursor, ">=", 2U) == 0) {
            relation = 1;
            parser->cursor += 2;
        } else if (*parser->cursor == '<') {
            relation = -2;
            ++parser->cursor;
        } else if (*parser->cursor == '>') {
            relation = 2;
            ++parser->cursor;
        } else {
            return true;
        }

        if (!minipp_expr_parse_shift(parser, &rhs)) {
            return false;
        }
        *value = minipp_expr_bool(minipp_expr_compare(*value,
                                                     rhs,
                                                     relation));
    }
}

static bool minipp_expr_parse_equality(MiniPpExprParser *parser,
                                       MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_relational(parser, value)) {
        return false;
    }
    for (;;) {
        bool equal;

        minipp_expr_skip_space(parser);
        if (strncmp(parser->cursor, "==", 2U) == 0) {
            equal = true;
        } else if (strncmp(parser->cursor, "!=", 2U) == 0) {
            equal = false;
        } else {
            return true;
        }
        parser->cursor += 2;
        if (!minipp_expr_parse_relational(parser, &rhs)) {
            return false;
        }
        if (equal) {
            *value = minipp_expr_bool(value->bits == rhs.bits);
        } else {
            *value = minipp_expr_bool(value->bits != rhs.bits);
        }
    }
}

static bool minipp_expr_parse_bit_and(MiniPpExprParser *parser,
                                      MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_equality(parser, value)) {
        return false;
    }
    for (;;) {
        minipp_expr_skip_space(parser);
        if (*parser->cursor != '&' || parser->cursor[1] == '&') {
            return true;
        }
        ++parser->cursor;
        if (!minipp_expr_parse_equality(parser, &rhs)) {
            return false;
        }
        value->bits &= rhs.bits;
        value->is_unsigned = value->is_unsigned || rhs.is_unsigned;
    }
}

static bool minipp_expr_parse_bit_xor(MiniPpExprParser *parser,
                                      MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_bit_and(parser, value)) {
        return false;
    }
    for (;;) {
        minipp_expr_skip_space(parser);
        if (*parser->cursor != '^') {
            return true;
        }
        ++parser->cursor;
        if (!minipp_expr_parse_bit_and(parser, &rhs)) {
            return false;
        }
        value->bits ^= rhs.bits;
        value->is_unsigned = value->is_unsigned || rhs.is_unsigned;
    }
}

static bool minipp_expr_parse_bit_or(MiniPpExprParser *parser,
                                     MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_bit_xor(parser, value)) {
        return false;
    }
    for (;;) {
        minipp_expr_skip_space(parser);
        if (*parser->cursor != '|' || parser->cursor[1] == '|') {
            return true;
        }
        ++parser->cursor;
        if (!minipp_expr_parse_bit_xor(parser, &rhs)) {
            return false;
        }
        value->bits |= rhs.bits;
        value->is_unsigned = value->is_unsigned || rhs.is_unsigned;
    }
}

static bool minipp_expr_parse_logical_and(MiniPpExprParser *parser,
                                          MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_bit_or(parser, value)) {
        return false;
    }
    while (true) {
        bool lhs_truth;

        minipp_expr_skip_space(parser);
        if (strncmp(parser->cursor, "&&", 2U) != 0) {
            return true;
        }
        parser->cursor += 2;
        lhs_truth = minipp_expr_truth(*value);
        if (!minipp_expr_parse_bit_or(parser, &rhs)) {
            return false;
        }
        *value = minipp_expr_bool(lhs_truth && minipp_expr_truth(rhs));
    }
}

static bool minipp_expr_parse_logical_or(MiniPpExprParser *parser,
                                         MiniPpInt *value) {
    MiniPpInt rhs;

    if (!minipp_expr_parse_logical_and(parser, value)) {
        return false;
    }
    while (true) {
        bool lhs_truth;

        minipp_expr_skip_space(parser);
        if (strncmp(parser->cursor, "||", 2U) != 0) {
            return true;
        }
        parser->cursor += 2;
        lhs_truth = minipp_expr_truth(*value);
        if (!minipp_expr_parse_logical_and(parser, &rhs)) {
            return false;
        }
        *value = minipp_expr_bool(lhs_truth || minipp_expr_truth(rhs));
    }
}

static bool minipp_expr_parse_conditional(MiniPpExprParser *parser,
                                          MiniPpInt *value) {
    MiniPpInt when_true;
    MiniPpInt when_false;
    bool condition;

    if (!minipp_expr_parse_logical_or(parser, value)) {
        return false;
    }
    minipp_expr_skip_space(parser);
    if (*parser->cursor != '?') {
        return true;
    }

    condition = minipp_expr_truth(*value);
    ++parser->cursor;
    if (!minipp_expr_parse_conditional(parser, &when_true)) {
        return false;
    }
    minipp_expr_skip_space(parser);
    if (*parser->cursor != ':') {
        return false;
    }
    ++parser->cursor;
    if (!minipp_expr_parse_conditional(parser, &when_false)) {
        return false;
    }
    *value = condition ? when_true : when_false;
    return true;
}

bool minipp_eval_if_expression(MiniPpState *state,
                               const char *expression,
                               bool *value) {
    MiniPpString defined_replaced;
    MiniPpString expanded;
    MiniPpExprParser parser;
    MiniPpInt result;
    bool ok = false;

    if (!minipp_replace_defined(state, expression, &defined_replaced)) {
        return false;
    }

    minipp_string_init(&expanded);
    if (!minipp_expand_text(state, defined_replaced.data, &expanded) ||
        !minipp_string_append_char(&expanded, '\0')) {
        fprintf(state->diagnostics, "minic-cpp: out-of-memory\n");
        goto done;
    }
    --expanded.size;

    parser.state = state;
    parser.cursor = expanded.data;
    if (!minipp_expr_parse_conditional(&parser, &result)) {
        fprintf(state->diagnostics,
                "minic-cpp: invalid-if-expression:%s",
                expression);
        goto done;
    }
    minipp_expr_skip_space(&parser);
    if (*parser.cursor != '\0') {
        fprintf(state->diagnostics,
                "minic-cpp: trailing-if-expression:%s",
                parser.cursor);
        goto done;
    }

    *value = minipp_expr_truth(result);
    ok = true;

done:
    minipp_string_destroy(&expanded);
    minipp_string_destroy(&defined_replaced);
    return ok;
}
