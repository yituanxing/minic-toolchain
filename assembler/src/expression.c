#include "minias_internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct MiniAsAddendParser {
    const char *cursor;
} MiniAsAddendParser;

static void skip_addend_space(MiniAsAddendParser *parser) {
    while (*parser->cursor == ' ' || *parser->cursor == '\t') {
        ++parser->cursor;
    }
}

static bool parse_addend_or(MiniAsAddendParser *parser, uint64_t *value);

static bool parse_addend_primary(MiniAsAddendParser *parser, uint64_t *value) {
    char *end = NULL;
    unsigned long long parsed;

    skip_addend_space(parser);
    if (*parser->cursor == '(') {
        ++parser->cursor;
        if (!parse_addend_or(parser, value)) {
            return false;
        }
        skip_addend_space(parser);
        if (*parser->cursor != ')') {
            return false;
        }
        ++parser->cursor;
        return true;
    }
    errno = 0;
    parsed = strtoull(parser->cursor, &end, 0);
    if (errno != 0 || end == parser->cursor) {
        return false;
    }
    parser->cursor = end;
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_addend_unary(MiniAsAddendParser *parser, uint64_t *value) {
    skip_addend_space(parser);
    if (*parser->cursor == '+') {
        ++parser->cursor;
        return parse_addend_unary(parser, value);
    }
    if (*parser->cursor == '-') {
        uint64_t operand;
        ++parser->cursor;
        if (!parse_addend_unary(parser, &operand)) {
            return false;
        }
        *value = UINT64_C(0) - operand;
        return true;
    }
    if (*parser->cursor == '~') {
        uint64_t operand;
        ++parser->cursor;
        if (!parse_addend_unary(parser, &operand)) {
            return false;
        }
        *value = ~operand;
        return true;
    }
    return parse_addend_primary(parser, value);
}

static bool parse_addend_mul(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_unary(parser, &result)) {
        return false;
    }
    for (;;) {
        char op;
        uint64_t rhs;

        skip_addend_space(parser);
        op = *parser->cursor;
        if (op != '*' && op != '/' && op != '%') {
            break;
        }
        ++parser->cursor;
        if (!parse_addend_unary(parser, &rhs)) {
            return false;
        }
        if (op == '*') {
            result *= rhs;
        } else {
            if (rhs == 0U) {
                return false;
            }
            result = op == '/' ? result / rhs : result % rhs;
        }
    }
    *value = result;
    return true;
}

static bool parse_addend_sum(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_mul(parser, &result)) {
        return false;
    }
    for (;;) {
        char op;
        uint64_t rhs;

        skip_addend_space(parser);
        op = *parser->cursor;
        if (op != '+' && op != '-') {
            break;
        }
        ++parser->cursor;
        if (!parse_addend_mul(parser, &rhs)) {
            return false;
        }
        result = op == '+' ? result + rhs : result - rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_shift(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_sum(parser, &result)) {
        return false;
    }
    for (;;) {
        bool left;
        uint64_t rhs;

        skip_addend_space(parser);
        if (strncmp(parser->cursor, "<<", 2U) == 0) {
            left = true;
        } else if (strncmp(parser->cursor, ">>", 2U) == 0) {
            left = false;
        } else {
            break;
        }
        parser->cursor += 2;
        if (!parse_addend_sum(parser, &rhs) || rhs >= 64U) {
            return false;
        }
        result = left ? result << (unsigned int)rhs
                      : result >> (unsigned int)rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_and(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_shift(parser, &result)) {
        return false;
    }
    for (;;) {
        uint64_t rhs;
        skip_addend_space(parser);
        if (*parser->cursor != '&' || parser->cursor[1] == '&') {
            break;
        }
        ++parser->cursor;
        if (!parse_addend_shift(parser, &rhs)) {
            return false;
        }
        result &= rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_xor(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_and(parser, &result)) {
        return false;
    }
    for (;;) {
        uint64_t rhs;
        skip_addend_space(parser);
        if (*parser->cursor != '^') {
            break;
        }
        ++parser->cursor;
        if (!parse_addend_and(parser, &rhs)) {
            return false;
        }
        result ^= rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_or(MiniAsAddendParser *parser, uint64_t *value) {
    uint64_t result;

    if (!parse_addend_xor(parser, &result)) {
        return false;
    }
    for (;;) {
        uint64_t rhs;
        skip_addend_space(parser);
        if (*parser->cursor != '|' || parser->cursor[1] == '|') {
            break;
        }
        ++parser->cursor;
        if (!parse_addend_xor(parser, &rhs)) {
            return false;
        }
        result |= rhs;
    }
    *value = result;
    return true;
}

static bool parse_addend_expression(const char *text, uint64_t *value) {
    MiniAsAddendParser parser;

    parser.cursor = text;
    if (!parse_addend_or(&parser, value)) {
        return false;
    }
    skip_addend_space(&parser);
    return *parser.cursor == '\0';
}

static bool is_symbol_start(char ch) {
    return ch == '.' || ch == '$' || ch == '_' || isalpha((unsigned char)ch);
}

static bool is_symbol_continue(char ch) {
    return is_symbol_start(ch) || isdigit((unsigned char)ch);
}

bool minias_parse_symbol_addend(const char *text, MiniAsSymbolExpr *expr) {
    const char *p;
    const char *start;
    size_t length;
    int sign = 1;
    uint64_t addend_bits = 0U;

    if (text == NULL || expr == NULL) {
        return false;
    }
    p = text;
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (!is_symbol_start(*p)) {
        return false;
    }
    start = p++;
    while (is_symbol_continue(*p)) {
        ++p;
    }
    length = (size_t)(p - start);
    if (length == 0U || length >= sizeof(expr->name)) {
        return false;
    }
    memcpy(expr->name, start, length);
    expr->name[length] = '\0';

    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '+' || *p == '-') {
        if (*p == '-') {
            sign = -1;
        }
        ++p;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        if (!parse_addend_expression(p, &addend_bits)) {
            return false;
        }
        p += strlen(p);
    }
    if (*p != '\0') {
        return false;
    }
    if (sign < 0) {
        addend_bits = UINT64_C(0) - addend_bits;
    }
    expr->addend = (int64_t)addend_bits;
    return true;
}
