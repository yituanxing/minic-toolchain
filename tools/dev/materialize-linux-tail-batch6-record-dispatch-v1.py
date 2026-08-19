#!/usr/bin/env python3
from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()
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
p.write_text(text)
print('materialized canonical static record-array dispatch')
