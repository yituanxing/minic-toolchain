#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


# GCC documents __extension__ as diagnostic-only. Keep it out of MinicType entirely: consume
# it at the shared declaration/type-specifier entry and let the ordinary type parser own every
# semantic/ABI decision that follows.
replace_once(
    "src/frontend/parser_type.c",
    '#include "frontend/parser_internal.h"\n',
    '#include "frontend/parser_internal.h"\n\n#include <string.h>\n',
)
replace_once(
    "src/frontend/parser_type.c",
    "static bool minic_parser_is_integer_type_specifier(MinicTokenKind kind) {\n",
    r'''static bool minic_parser_current_identifier_is(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool minic_parser_skip_gnu_extension_markers(MinicParser *parser) {
    while (minic_parser_current_identifier_is(parser, "__extension__")) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

static bool minic_parser_is_integer_type_specifier(MinicTokenKind kind) {
''',
)
replace_once(
    "src/frontend/parser_type.c",
    """    if (type == NULL) {
        minic_parser_error(parser, "internal error: missing parsed type output");
        return false;
    }

    is_const = false;
""",
    """    if (type == NULL) {
        minic_parser_error(parser, "internal error: missing parsed type output");
        return false;
    }
    if (!minic_parser_skip_gnu_extension_markers(parser)) {
        return false;
    }

    is_const = false;
""",
)

print("staged GNU __extension__ as diagnostic-only declaration/type marker")
