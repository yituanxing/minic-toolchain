#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:100]!r}")
    target.write_text(text.replace(old, new, 1))


# Parameter names are optional in function declarations. Parse the signature first
# without requiring names; if the declarator turns out to be a definition, enforce
# names before creating parameter locals.
replace_once(
    "src/frontend/parser_function.c",
    '''        !minic_parser_parse_parameter_list(\n            parser, parameter_name_spans, parameter_types, &parameter_count, true, &is_variadic) ||\n''',
    '''        !minic_parser_parse_parameter_list(\n            parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||\n''',
)

replace_once(
    "src/frontend/parser_function.c",
    '''    if (parser->current.kind != MINIC_TOKEN_LBRACE) {\n        minic_parser_error(parser, "expected ';' or '{' after function declarator");\n        return false;\n    }\n''',
    '''    if (parser->current.kind != MINIC_TOKEN_LBRACE) {\n        minic_parser_error(parser, "expected ';' or '{' after function declarator");\n        return false;\n    }\n    {\n        size_t parameter_index;\n\n        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {\n            if (minic_parser_span_length(parameter_name_spans[parameter_index]) == 0U) {\n                minic_parser_error(parser, "function definition requires parameter names");\n                return false;\n            }\n        }\n    }\n''',
)

print("staged unnamed parameters in function declarations")
