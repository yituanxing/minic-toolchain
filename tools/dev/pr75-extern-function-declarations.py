#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_function.c",
    """    if (is_internal &&
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'")) {
        return false;
    }
""",
    """    if (is_internal &&
        !minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'")) {
        return false;
    }
    if (!is_internal && parser->current.kind == MINIC_TOKEN_KW_EXTERN &&
        !minic_parser_advance(parser)) {
        return false;
    }
""",
)

replace_once(
    "src/frontend/parser_function.c",
    """    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected function name");
        return false;
    }

    name_span = parser->current.span;
    function_id = minic_parser_find_function(parser, name_span);
""",
    """    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected function name in parenthesized declarator");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized function name")) {
            return false;
        }
    } else {
        minic_parser_error(parser, "expected function name");
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
""",
)

replace_once(
    "src/frontend/parser_function.c",
    """    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
""",
    """    if (!is_internal && parser->current.kind != MINIC_TOKEN_LPAREN) {
""",
)

marker = "bool minic_parse_c0_program(const char *path,\n"
helper = r'''static bool extern_declaration_is_function(MinicParser *parser, bool *is_function) {
    MinicParser probe;
    MinicType declared_type;

    if (parser == NULL || is_function == NULL ||
        parser->current.kind != MINIC_TOKEN_KW_EXTERN) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe) || !minic_parser_parse_type_name(&probe, &declared_type)) {
        return false;
    }
    (void)declared_type;

    if (probe.current.kind == MINIC_TOKEN_IDENTIFIER) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        *is_function = probe.current.kind == MINIC_TOKEN_LPAREN;
        return true;
    }
    if (probe.current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
            *is_function = false;
            return true;
        }
        if (!minic_parser_advance(&probe) ||
            !minic_parser_expect(
                &probe, MINIC_TOKEN_RPAREN, "expected ')' in extern declarator probe")) {
            return false;
        }
        *is_function = probe.current.kind == MINIC_TOKEN_LPAREN;
        return true;
    }

    minic_parser_error(parser, "expected extern declaration name");
    return false;
}

'''
path = Path("src/frontend/parser_function.c")
text = path.read_text()
if text.count(marker) != 1:
    raise SystemExit("unexpected minic_parse_c0_program marker")
path.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/parser_function.c",
    """        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {
            success = minic_parser_parse_extern_global(&parser);
""",
    """        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {
            bool is_function;

            if (!extern_declaration_is_function(&parser, &is_function)) {
                success = false;
            } else if (is_function) {
                success = parse_function(&parser, false);
            } else {
                success = minic_parser_parse_extern_global(&parser);
            }
""",
)

print("staged extern function declarations and parenthesized function names")
