#include "frontend/parser_internal.h"

#include <limits.h>

static int hexadecimal_digit_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool parse_character_value(MinicParser *parser, int *value) {
    MinicSourceSpan span;
    size_t length;
    size_t offset;
    char character;

    span = parser->current.span;
    length = span.end.offset - span.begin.offset;
    if (length != 3U && length != 4U) {
        minic_parser_error(parser, "invalid character constant");
        return false;
    }
    offset = span.begin.offset + 1U;
    character = parser->source[offset];
    if (character != '\\') {
        *value = (int)(unsigned char)character;
        return minic_parser_advance(parser);
    }

    character = parser->source[offset + 1U];
    switch (character) {
    case '0':
        *value = 0;
        break;
    case 'a':
        *value = '\a';
        break;
    case 'b':
        *value = '\b';
        break;
    case 'f':
        *value = '\f';
        break;
    case 'n':
        *value = '\n';
        break;
    case 'r':
        *value = '\r';
        break;
    case 't':
        *value = '\t';
        break;
    case 'v':
        *value = '\v';
        break;
    case '\\':
        *value = '\\';
        break;
    case '\'':
        *value = '\'';
        break;
    case '"':
        *value = '"';
        break;
    case '?':
        *value = '?';
        break;
    default:
        minic_parser_error(parser, "unsupported character escape");
        return false;
    }
    return minic_parser_advance(parser);
}

static size_t integer_digit_end(const MinicParser *parser, MinicSourceSpan span) {
    size_t end;

    end = span.end.offset;
    while (end > span.begin.offset) {
        char character;

        character = parser->source[end - 1U];
        if (character != 'l' && character != 'L' && character != 'u' && character != 'U') {
            break;
        }
        end -= 1U;
    }
    return end;
}

bool minic_parser_parse_integer_value(MinicParser *parser, int *value) {
    MinicSourceSpan span;
    size_t digit_end;
    size_t offset;
    unsigned long parsed;
    unsigned long base;
    bool constant_kind;

    constant_kind = parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT ||
                    parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT;
    if (value == NULL || !constant_kind) {
        minic_parser_error(parser, "expected integer or character constant");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        return parse_character_value(parser, value);
    }

    span = parser->current.span;
    digit_end = integer_digit_end(parser, span);
    offset = span.begin.offset;
    base = 10UL;
    if (digit_end - span.begin.offset >= 2U && parser->source[offset] == '0' &&
        (parser->source[offset + 1U] == 'x' || parser->source[offset + 1U] == 'X')) {
        base = 16UL;
        offset += 2U;
    }

    parsed = 0UL;
    for (; offset < digit_end; ++offset) {
        int digit_value;
        unsigned long digit;

        digit_value = hexadecimal_digit_value(parser->source[offset]);
        if (digit_value < 0 || (unsigned long)digit_value >= base) {
            minic_parser_error(parser, "invalid integer constant digit");
            return false;
        }
        digit = (unsigned long)digit_value;
        if (parsed > ((unsigned long)INT_MAX - digit) / base) {
            minic_parser_error(parser, "integer constant exceeds current literal range");
            return false;
        }
        parsed = parsed * base + digit;
    }

    *value = (int)parsed;
    return minic_parser_advance(parser);
}
