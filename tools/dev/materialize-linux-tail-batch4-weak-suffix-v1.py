#!/usr/bin/env python3
from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()

def one(old: str, new: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f'expected exactly one replacement, found {count}')
    text = text.replace(old, new, 1)

one(
'''        bool declarator_has_visibility;\n        bool is_array;''',
'''        bool declarator_has_visibility;\n        bool declarator_is_weak;\n        bool is_array;''')
one(
'''        declarator_has_visibility = has_visibility;\n        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));''',
'''        declarator_has_visibility = has_visibility;\n        declarator_is_weak = false;\n        (void)memset(declarator_section_name, 0, sizeof(declarator_section_name));''')
one(
'''        if (!minic_parser_parse_gnu_object_attribute_lists_with_visibility(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility)) {''',
'''        if (!minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility,\n                &declarator_is_weak)) {''')
one(
'''            !minic_parser_parse_gnu_object_attribute_lists_with_visibility(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility)) {''',
'''            !minic_parser_parse_gnu_object_attribute_lists_with_symbol_metadata(\n                parser,\n                declarator_section_name,\n                sizeof(declarator_section_name),\n                &declarator_section_name_length,\n                &declarator_has_section,\n                &declarator_explicit_alignment,\n                &declarator_visibility,\n                &declarator_has_visibility,\n                &declarator_is_weak)) {''')
one(
'''        parser->program->global_objects[object_id].is_block_scope_extern_only = false;\n\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {''',
'''        parser->program->global_objects[object_id].is_block_scope_extern_only = false;\n        if (declarator_is_weak &&\n            !minic_c0_global_object_set_weak(parser->program, object_id, true)) {\n            minic_parser_error(parser, "GNU weak requires external object linkage");\n            return false;\n        }\n\n        if (parser->current.kind != MINIC_TOKEN_COMMA) {''')

p.write_text(text)
print('materialized weak extern suffix metadata')
