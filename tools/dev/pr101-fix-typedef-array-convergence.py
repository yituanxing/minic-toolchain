#!/usr/bin/env python3
from pathlib import Path

p = Path("src/frontend/parser_typedef.c")
text = p.read_text()

old = '''    MinicType aliased_type;\n    MinicTypeAliasId alias_id;\n    size_t bounds[8];\n    size_t bound_count;\n    bool is_function_declarator;\n\n    bound_count = 0U;\n    is_function_declarator = false;\n'''
new = '''    MinicType aliased_type;\n    MinicTypeAliasId alias_id;\n    bool is_function_declarator;\n\n    is_function_declarator = false;\n'''
if text.count(old) != 1:
    raise SystemExit(f"typedef array state anchor count={text.count(old)}")
text = text.replace(old, new, 1)

old = '''    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        if (is_function_declarator) {\n            minic_parser_error(parser, "function typedef array declarators are not supported yet");\n            return false;\n        }\n        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {\n            minic_parser_error(parser, "at most eight array dimensions are supported");\n            return false;\n        }\n        if (!minic_parser_advance(parser) ||\n            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {\n            return false;\n        }\n        bound_count += 1U;\n    }\n\n    while (bound_count > 0U) {\n        bound_count -= 1U;\n        if (!minic_c0_program_add_array_type(\n                parser->program, aliased_type, bounds[bound_count], &aliased_type)) {\n            minic_parser_error(parser, "out of memory while building typedef array type");\n            return false;\n        }\n    }\n'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        bool is_array;\n\n        if (is_function_declarator) {\n            minic_parser_error(parser, "function typedef array declarators are not supported yet");\n            return false;\n        }\n        if (!minic_parser_parse_array_declarator_suffix(\n                parser, aliased_type, false, &aliased_type, &is_array) || !is_array) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {\n                minic_parser_error(parser, "cannot build typedef array declarator type");\n            }\n            return false;\n        }\n    }\n'''
if text.count(old) != 1:
    raise SystemExit(f"typedef array parser anchor count={text.count(old)}")
text = text.replace(old, new, 1)
p.write_text(text)
