#!/usr/bin/env python3
from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()

# All record arrays, fixed or inferred, belong to the dedicated record-array
# entity lifecycle owner. The generic fixed-array path used to bypass it.
old = '''    if (minic_type_is_record(element_type) && parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_record(parser,
                                   element_type,
                                   name_span,
                                   section_name,
                                   section_capacity,
                                   section_name_length,
                                   has_section,
                                   explicit_alignment);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
'''
new = '''    if (minic_type_is_record(element_type)) {
        return parse_static_record(parser,
                                   element_type,
                                   name_span,
                                   section_name,
                                   section_capacity,
                                   section_name_length,
                                   has_section,
                                   explicit_alignment);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {
'''
if text.count(old) != 1:
    raise SystemExit(f'expected record dispatch prelude once, found {text.count(old)}')
text = text.replace(old, new, 1)
old = '''        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_record(element_type)) {
                return parse_static_record(parser,
                                           element_type,
                                           name_span,
                                           section_name,
                                           section_capacity,
                                           section_name_length,
                                           has_section,
                                           explicit_alignment);
            }
            if (minic_type_is_char_integer(element_type)) {
'''
new = '''        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_char_integer(element_type)) {
'''
if text.count(old) != 1:
    raise SystemExit(f'expected inferred record dispatch once, found {text.count(old)}')
text = text.replace(old, new, 1)

# A file-scope incomplete pointer array declaration is a tentative definition;
# it may be completed by a later initializer in the same translation unit.
old = '''        if (inferred_bound) {
            minic_parser_error(parser, "incomplete static pointer array requires an initializer");
            return false;
        }
        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
'''
new = '''        if (existing_id == MINIC_GLOBAL_OBJECT_INVALID) {
'''
if text.count(old) != 1:
    raise SystemExit(f'expected incomplete pointer-array rejection once, found {text.count(old)}')
text = text.replace(old, new, 1)

p.write_text(text)
print('materialized canonical static array lifecycle dispatch')
