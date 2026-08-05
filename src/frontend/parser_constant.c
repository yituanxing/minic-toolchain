#include "frontend/parser_internal.h"

#include <limits.h>

static int hexadecimal_digit_value(char character)
{
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

bool minic_parser_parse_integer_value(
    MinicParser *parser,
    int *value)
{
    MinicSourceSpan span;
    size_t offset;
    unsigned long parsed;
    unsigned long base;

    if (value == NULL ||
        parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_error(parser, "expected integer constant");
        return false;
    }

    span = parser->current.span;
    offset = span.begin.offset;
    base = 10UL;
    if (span.end.offset - span.begin.offset >= 2U &&
        parser->source[offset] == '0' &&
        (parser->source[offset + 1U] == 'x' ||
         parser->source[offset + 1U] == 'X')) {
        base = 16UL;
        offset += 2U;
    }

    parsed = 0UL;
    for (; offset < span.end.offset; ++offset) {
        int digit_value;
        unsigned long digit;

        digit_value = hexadecimal_digit_value(parser->source[offset]);
        if (digit_value < 0 || (unsigned long)digit_value >= base) {
            minic_parser_error(parser, "invalid integer constant digit");
            return false;
        }
        digit = (unsigned long)digit_value;
        if (parsed > ((unsigned long)INT_MAX - digit) / base) {
            minic_parser_error(parser, "integer constant exceeds C0 int range");
            return false;
        }
        parsed = parsed * base + digit;
    }

    *value = (int)parsed;
    return minic_parser_advance(parser);
}
