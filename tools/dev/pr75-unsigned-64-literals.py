#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);\n",
    "bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);\n"
    "bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value);\n",
    "unsigned integer parser declaration",
)

path = Path("src/frontend/parser_constant.c")
text = path.read_text()
marker = "bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value) {\n"
if text.count(marker) != 1:
    raise SystemExit(f"signed 64-bit integer parser marker count={text.count(marker)}")
helper = r'''bool minic_parser_parse_unsigned_integer_value64(MinicParser *parser, uint64_t *value) {
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

'''
text = text.replace(marker, helper + marker, 1)
path.write_text(text)

path = Path("src/frontend/parser_expression.c")
text = path.read_text()
old = '''    } else if (!minic_parser_parse_integer_value64(parser, &value)) {
        return false;
    }
    expression.value.integer_value = value;
'''
new = '''    } else if (minic_type_is_unsigned_integer(literal_type)) {
        uint64_t unsigned_value;

        if (!minic_parser_parse_unsigned_integer_value64(parser, &unsigned_value)) {
            return false;
        }
        (void)memcpy(&value, &unsigned_value, sizeof(value));
    } else if (!minic_parser_parse_integer_value64(parser, &value)) {
        return false;
    }
    expression.value.integer_value = value;
'''
if text.count(old) != 1:
    raise SystemExit(f"ordinary integer literal parser branch count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged full-width unsigned 64-bit integer literals")
