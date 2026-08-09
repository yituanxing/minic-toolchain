#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/token.h",
    """    MINIC_TOKEN_KW_FLOAT,
    MINIC_TOKEN_KW_INT,
    MINIC_TOKEN_KW_LONG,
""",
    """    MINIC_TOKEN_KW_FLOAT,
    MINIC_TOKEN_KW_INLINE,
    MINIC_TOKEN_KW_INT,
    MINIC_TOKEN_KW_LONG,
""",
)

replace_once(
    "src/frontend/lexer.c",
    """    if (length == 5U && memcmp(text, \"float\", 5U) == 0) {
        return MINIC_TOKEN_KW_FLOAT;
    }
    if (length == 3U && memcmp(text, \"int\", 3U) == 0) {
""",
    """    if (length == 5U && memcmp(text, \"float\", 5U) == 0) {
        return MINIC_TOKEN_KW_FLOAT;
    }
    if (length == 6U && memcmp(text, \"inline\", 6U) == 0) {
        return MINIC_TOKEN_KW_INLINE;
    }
    if (length == 3U && memcmp(text, \"int\", 3U) == 0) {
""",
)

replace_once(
    "src/frontend/token.c",
    """    case MINIC_TOKEN_KW_FLOAT:
        return \"float\";
    case MINIC_TOKEN_KW_INT:
""",
    """    case MINIC_TOKEN_KW_FLOAT:
        return \"float\";
    case MINIC_TOKEN_KW_INLINE:
        return \"inline\";
    case MINIC_TOKEN_KW_INT:
""",
)

replace_once(
    "src/frontend/parser_function.c",
    """static bool parse_function(MinicParser *parser, bool is_internal) {
    MinicSourceSpan name_span;
""",
    """static bool parse_function(MinicParser *parser, bool is_internal) {
    MinicSourceSpan name_span;
""",
)
# Consume `static` and `inline` in the supported function-specifier orders.  Inline is
# semantically an optimization/linkage hint here; code generation remains unchanged.
replace_once(
    "src/frontend/parser_function.c",
    """    if (is_internal &&
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, \"expected keyword 'static'\")) {
        return false;
    }
    if (!minic_parser_parse_type_name(parser, &return_type)) {
""",
    """    if (is_internal &&
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, \"expected keyword 'static'\")) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_KW_INLINE && !minic_parser_advance(parser)) {
        return false;
    } else if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_INLINE) {
        return false;
    }
    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_STATIC) {
        is_internal = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_parser_parse_type_name(parser, &return_type)) {
""",
)
# The first conditional above consumes inline in both internal and external calls; the
# `else if` is intentionally unreachable after a successful advance, so simplify it to
# a clear one-pass specifier sequence.
replace_once(
    "src/frontend/parser_function.c",
    """    if (parser->current.kind == MINIC_TOKEN_KW_INLINE && !minic_parser_advance(parser)) {
        return false;
    } else if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_INLINE) {
        return false;
    }
    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_STATIC) {
""",
    """    if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_STATIC) {
""",
)
# Allow `inline static` too: after consuming static, accept no second inline because duplicate
# function specifiers are outside the current boundary; static inline is already handled by
# the internal-call path.
replace_once(
    "src/frontend/parser_function.c",
    """    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
        return parse_external_pointer_definition(parser, return_type, name_span);
    }
""",
    """    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON ||
            parser->current.kind == MINIC_TOKEN_EQUAL ||
            parser->current.kind == MINIC_TOKEN_LBRACKET) {
            minic_parser_error(parser, \"inline specifier requires a function declarator\");
            return false;
        }
        return parse_external_pointer_definition(parser, return_type, name_span);
    }
""",
)
# The object diagnostic above must only trigger when parse_function was entered through
# `inline`, not for ordinary external pointer definitions. Track that fact explicitly.
replace_once(
    "src/frontend/parser_function.c",
    """    bool is_main;
    bool is_variadic;

    body_block = MINIC_BLOCK_INVALID;
""",
    """    bool is_inline;
    bool is_main;
    bool is_variadic;

    body_block = MINIC_BLOCK_INVALID;
""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    parameter_count = 0U;
    is_variadic = false;
""",
    """    parameter_count = 0U;
    is_inline = false;
    is_variadic = false;
""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
""",
    """    if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
        is_inline = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
""",
)
replace_once(
    "src/frontend/parser_function.c",
    """    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (parser->current.kind == MINIC_TOKEN_SEMICOLON ||
            parser->current.kind == MINIC_TOKEN_EQUAL ||
            parser->current.kind == MINIC_TOKEN_LBRACKET) {
            minic_parser_error(parser, \"inline specifier requires a function declarator\");
            return false;
        }
        return parse_external_pointer_definition(parser, return_type, name_span);
    }
""",
    """    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (is_inline) {
            minic_parser_error(parser, \"inline specifier requires a function declarator\");
            return false;
        }
        return parse_external_pointer_definition(parser, return_type, name_span);
    }
""",
)

# Static declaration probing must skip the inline function specifier before parsing the type.
replace_once(
    "src/frontend/parser_function.c",
    """    probe = *parser;
    if (!minic_parser_advance(&probe) || !minic_parser_parse_type_name(&probe, &declared_type)) {
        return false;
    }
""",
    """    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_KW_INLINE && !minic_parser_advance(&probe)) {
        return false;
    }
    if (!minic_parser_parse_type_name(&probe, &declared_type)) {
        return false;
    }
""",
)

# Top-level `inline` declarations are functions; object forms are rejected inside parse_function.
replace_once(
    "src/frontend/parser_function.c",
    """        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            bool is_function;
""",
    """        } else if (parser.current.kind == MINIC_TOKEN_KW_INLINE) {
            success = parse_function(&parser, false);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            bool is_function;
""",
)

print("staged C99 inline function specifier parsing")
