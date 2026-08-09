#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    "        int integer_value;\n",
    "        int64_t integer_value;\n",
)
replace_once(
    "src/frontend/parser_internal.h",
    "bool minic_parser_parse_integer_value(MinicParser *parser, int *value);\n",
    """bool minic_parser_parse_integer_value(MinicParser *parser, int *value);
bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value);
""",
)

# Keep the existing narrow parser for array bounds/enums. Add a wide expression-literal
# parser so this capability does not silently widen unrelated constant-expression contracts.
path = Path("src/frontend/parser_constant.c")
text = path.read_text()
append_marker = "bool minic_parser_parse_integer_value(MinicParser *parser, int *value) {\n"
start = text.index(append_marker)
# Insert the wide helper before the existing public narrow parser.
wide = r'''bool minic_parser_parse_integer_value64(MinicParser *parser, int64_t *value) {
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

'''
path.write_text(text[:start] + wide + text[start:])

replace_once(
    "src/frontend/parser_expression.c",
    """    MinicType literal_type;
    int value;

    span = parser->current.span;
""",
    """    MinicType literal_type;
    int64_t value;

    span = parser->current.span;
""",
)
replace_once(
    "src/frontend/parser_expression.c",
    """    if (!minic_parser_parse_integer_value(parser, &value)) {
        return false;
    }
    expression.value.integer_value = value;
""",
    """    if (parser->current.kind == MINIC_TOKEN_CHARACTER_CONSTANT) {
        int character_value;

        if (!minic_parser_parse_integer_value(parser, &character_value)) {
            return false;
        }
        value = (int64_t)character_value;
    } else if (!minic_parser_parse_integer_value64(parser, &value)) {
        return false;
    }
    expression.value.integer_value = value;
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """    case MINIC_EXPRESSION_INTEGER:
        return fprintf(file, "  li a0, %d\\n", expression->value.integer_value) >= 0 &&
""",
    """    case MINIC_EXPRESSION_INTEGER:
        return fprintf(file, "  li a0, %" PRId64 "\\n", expression->value.integer_value) >= 0 &&
""",
)

print("staged signed 64-bit integer literal payload and RV64 emission")
