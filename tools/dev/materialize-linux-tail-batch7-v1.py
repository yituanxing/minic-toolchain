#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    p.write_text(text.replace(old, new, 1))


replace_once(
    'src/frontend/parser_statement.c',
    '''    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type)) {\n        return false;\n    }\n    if (local_declarator_starts_function_pointer(parser)) {\n''',
    '''    if (!minic_parser_parse_pointer_declarator(parser, base_type, &declared_type) ||\n        !parse_local_object_attributes(parser, &attributes)) {\n        return false;\n    }\n    if (local_declarator_starts_function_pointer(parser)) {\n''',
    'local interposed object attributes',
)

replace_once(
    'src/frontend/parser_statement.c',
    '''    if (!minic_parser_parse_local_storage_class(parser, &is_register_storage) ||\n        !minic_parser_parse_type_specifiers(parser, &base_type) ||\n        !parse_local_declaration_head_attributes(parser)) {\n        return false;\n    }\n\n    for (;;) {\n''',
    '''    if (!minic_parser_parse_local_storage_class(parser, &is_register_storage) ||\n        !minic_parser_parse_type_specifiers(parser, &base_type) ||\n        !parse_local_declaration_head_attributes(parser)) {\n        return false;\n    }\n    if (parser->current.kind == MINIC_TOKEN_SEMICOLON &&\n        (minic_type_is_record(base_type) || minic_type_is_enum(base_type))) {\n        return minic_parser_advance(parser);\n    }\n\n    for (;;) {\n''',
    'block-scope type-only declaration',
)

replace_once(
    'src/frontend/parser_record.c',
    '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        if (record_field_starts_parenthesized_pointer_array(parser)) {\n            if (!parse_pointer_to_array_field_declarator(\n                    parser, field_type, &name_span, &field_type)) {\n                return false;\n            }\n        } else if (!parse_function_pointer_field_declarator(\n                       parser, field_type, &name_span, &field_type)) {\n            return false;\n        }\n    } else {\n        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {\n            minic_parser_error(parser, "expected record field name");\n            return false;\n        }\n        name_span = parser->current.span;\n        if (!minic_parser_advance(parser)) {\n            return false;\n        }\n    }\n''',
    '''    if (parser->current.kind == MINIC_TOKEN_LPAREN &&\n        record_field_starts_parenthesized_pointer_array(parser)) {\n        if (!parse_pointer_to_array_field_declarator(\n                parser, field_type, &name_span, &field_type)) {\n            return false;\n        }\n    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {\n        MinicParser probe;\n\n        probe = *parser;\n        if (!minic_parser_advance(&probe)) {\n            return false;\n        }\n        if (probe.current.kind == MINIC_TOKEN_STAR) {\n            if (!parse_function_pointer_field_declarator(\n                    parser, field_type, &name_span, &field_type)) {\n                return false;\n            }\n        } else if (!minic_parser_parse_direct_declarator_name(parser, &name_span)) {\n            return false;\n        }\n    } else if (!minic_parser_parse_direct_declarator_name(parser, &name_span)) {\n        return false;\n    }\n''',
    'parenthesized direct record field declarator',
)

run = Path('tests/compiler/c0/run.sh')
text = run.read_text()
needle = 'run-linux-tail-batch7.sh'
if needle not in text:
    text += '''\nMINIC="$minic" \\\nBUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \\\nsh "$root/tests/compiler/c0/run-linux-tail-batch7.sh"\n'''
    run.write_text(text)

print('materialized Linux tail batch7 parser convergence')
