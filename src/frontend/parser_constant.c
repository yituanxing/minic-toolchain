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
    if (length < 3U) {
        minic_parser_error(parser, "invalid character constant");
        return false;
    }
    offset = span.begin.offset + 1U;
    character = parser->source[offset];
    if (character != '\\') {
        if (length != 3U) {
            minic_parser_error(parser, "invalid character constant");
            return false;
        }
        *value = (int)(unsigned char)character;
        return minic_parser_advance(parser);
    }

    character = parser->source[offset + 1U];
    if (character == 'x') {
        unsigned int parsed;
        size_t digit_offset;

        if (length < 5U) {
            minic_parser_error(parser, "invalid hexadecimal character escape");
            return false;
        }
        parsed = 0U;
        for (digit_offset = offset + 2U; digit_offset + 1U < span.end.offset; ++digit_offset) {
            int digit_value;

            digit_value = hexadecimal_digit_value(parser->source[digit_offset]);
            if (digit_value < 0 ||
                parsed > ((unsigned int)UCHAR_MAX - (unsigned int)digit_value) / 16U) {
                minic_parser_error(parser, "hexadecimal character escape is out of range");
                return false;
            }
            parsed = parsed * 16U + (unsigned int)digit_value;
        }
        *value = (int)parsed;
        return minic_parser_advance(parser);
    }
    if (character >= '0' && character <= '7') {
        unsigned int parsed;
        unsigned int digit_count;
        size_t digit_offset;

        parsed = 0U;
        digit_count = 0U;
        digit_offset = offset + 1U;
        while (digit_offset + 1U < span.end.offset && digit_count < 3U &&
               parser->source[digit_offset] >= '0' && parser->source[digit_offset] <= '7') {
            unsigned int digit;

            digit = (unsigned int)(parser->source[digit_offset] - '0');
            if (parsed > ((unsigned int)UCHAR_MAX - digit) / 8U) {
                minic_parser_error(parser, "octal character escape is out of range");
                return false;
            }
            parsed = parsed * 8U + digit;
            digit_count += 1U;
            digit_offset += 1U;
        }
        if (digit_count == 0U || digit_offset + 1U != span.end.offset) {
            minic_parser_error(parser, "invalid octal character escape");
            return false;
        }
        *value = (int)parsed;
        return minic_parser_advance(parser);
    }
    if (length != 4U) {
        minic_parser_error(parser, "invalid character escape");
        return false;
    }
    switch (character) {
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

bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value) {
    MinicSourceSpan span;
    size_t digit_end;
    size_t offset;
    uint64_t parsed;
    uint64_t base;

    if (value == NULL || parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_error(parser, "expected integer constant");
        return false;
    }
    span = parser->current.span;
    digit_end = integer_digit_end(parser, span);
    offset = span.begin.offset;
    base = 10U;
    if (digit_end - span.begin.offset >= 2U && parser->source[offset] == '0' &&
        (parser->source[offset + 1U] == 'x' || parser->source[offset + 1U] == 'X')) {
        base = 16U;
        offset += 2U;
    }

    parsed = 0U;
    for (; offset < digit_end; ++offset) {
        int digit_value;
        uint64_t digit;

        digit_value = hexadecimal_digit_value(parser->source[offset]);
        if (digit_value < 0 || (uint64_t)digit_value >= base) {
            minic_parser_error(parser, "invalid integer constant digit");
            return false;
        }
        digit = (uint64_t)digit_value;
        if (parsed > (UINT64_MAX - digit) / base) {
            minic_parser_error(parser, "integer constant exceeds unsigned 64-bit literal range");
            return false;
        }
        parsed = parsed * base + digit;
    }
    *value = parsed;
    return minic_parser_advance(parser);
}

bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value) {
    MinicSourceSpan span;
    size_t digit_end;
    size_t offset;
    uint64_t parsed;
    uint64_t base;

    if (value == NULL || parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_error(parser, "expected integer constant");
        return false;
    }
    span = parser->current.span;
    digit_end = integer_digit_end(parser, span);
    offset = span.begin.offset;
    base = 10U;
    if (digit_end - span.begin.offset >= 2U && parser->source[offset] == '0' &&
        (parser->source[offset + 1U] == 'x' || parser->source[offset + 1U] == 'X')) {
        base = 16U;
        offset += 2U;
    }

    parsed = 0U;
    for (; offset < digit_end; ++offset) {
        int digit_value;
        uint64_t digit;

        digit_value = hexadecimal_digit_value(parser->source[offset]);
        if (digit_value < 0 || (uint64_t)digit_value >= base) {
            minic_parser_error(parser, "invalid integer constant digit");
            return false;
        }
        digit = (uint64_t)digit_value;
        if (parsed > ((uint64_t)INT64_MAX - digit) / base) {
            minic_parser_error(parser, "integer constant exceeds signed 64-bit literal range");
            return false;
        }
        parsed = parsed * base + digit;
    }
    *value = (int64_t)parsed;
    return minic_parser_advance(parser);
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
