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
    '''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
''',
    '''bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span);
bool minic_parser_parse_string_literal_size(MinicParser *parser, uint64_t *size);
''',
    "string literal size declaration",
)

path = Path("src/frontend/parser_string.c")
text = path.read_text()
marker = "static bool\nadd_string_payload(MinicParser *parser, MinicSourceSpan span, MinicGlobalObjectId object_id) {\n"
helper = r'''bool minic_parser_parse_string_literal_size(MinicParser *parser, uint64_t *size) {
    size_t decoded_length;
    size_t total_length;

    if (parser == NULL || size == NULL || parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    total_length = 0U;
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(parser, parser->current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "concatenated string literal is too long");
            }
            return false;
        }
        total_length += decoded_length;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX) {
        minic_parser_error(parser, "string literal sizeof result is too large");
        return false;
    }
    *size = (uint64_t)(total_length + 1U);
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"string payload marker: expected 1 match, found {text.count(marker)}")
path.write_text(text.replace(marker, helper + marker, 1))

path = Path("src/frontend/parser_core.c")
text = path.read_text()
old = '''static bool parse_array_bound_sizeof(MinicParser *parser, int64_t *value) {
    MinicType measured_type;
    uint64_t measured_size;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_SIZEOF, "expected 'sizeof'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after sizeof") ||
        !minic_parser_parse_type_name(parser, &measured_type) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after sizeof type")) {
        return false;
    }
    if (!array_bound_type_size(parser->program, measured_type, &measured_size)) {
        minic_parser_error(parser, "unsupported sizeof type in array bound constant expression");
        return false;
    }
'''
new = '''static bool parse_array_bound_sizeof(MinicParser *parser, int64_t *value) {
    MinicType measured_type;
    uint64_t measured_size;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_SIZEOF, "expected 'sizeof'") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after sizeof")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!minic_parser_parse_string_literal_size(parser, &measured_size) ||
            !minic_parser_expect(parser,
                                 MINIC_TOKEN_RPAREN,
                                 "expected ')' after sizeof string literal")) {
            return false;
        }
    } else {
        if (!minic_parser_parse_type_name(parser, &measured_type) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after sizeof type") ||
            !array_bound_type_size(parser->program, measured_type, &measured_size)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser,
                                   "unsupported sizeof type in array bound constant expression");
            }
            return false;
        }
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"array-bound sizeof helper: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))

print("staged sizeof(string literal) folding in fixed array bounds")
