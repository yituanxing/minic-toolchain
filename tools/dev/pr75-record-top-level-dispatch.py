#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


path = Path("src/frontend/parser_function.c")
text = path.read_text()
marker = "bool minic_parse_c0_program(const char *path,\n"
helper = r'''static bool record_keyword_starts_standalone_declaration(MinicParser *parser,
                                                         bool *is_standalone) {
    MinicParser probe;
    size_t token_length;

    if (parser == NULL || is_standalone == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag or definition after record keyword");
        return false;
    }

    token_length = minic_parser_span_length(probe.current.span);
    if (token_length == 13U &&
        memcmp(parser->source + probe.current.span.begin.offset, "__attribute__", 13U) == 0) {
        *is_standalone = true;
        return true;
    }

    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_standalone = probe.current.kind == MINIC_TOKEN_SEMICOLON ||
                     probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit("unexpected minic_parse_c0_program marker")
path.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/parser_function.c",
    """        } else if (parser.current.kind == MINIC_TOKEN_KW_STRUCT ||
                   parser.current.kind == MINIC_TOKEN_KW_UNION) {
            success = minic_parser_parse_record_definition(&parser);
""",
    """        } else if (parser.current.kind == MINIC_TOKEN_KW_STRUCT ||
                   parser.current.kind == MINIC_TOKEN_KW_UNION) {
            bool is_standalone;

            if (!record_keyword_starts_standalone_declaration(&parser, &is_standalone)) {
                success = false;
            } else if (is_standalone) {
                success = minic_parser_parse_record_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
""",
)

print("staged top-level record tag/declarator dispatch")
