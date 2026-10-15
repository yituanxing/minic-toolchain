#!/usr/bin/env python3
"""Materialize inferred outer bounds for multidimensional static arrays."""
from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()

old = '''    {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_char_integer(element_type)) {
                return parse_static_inferred_char_array(parser,
                                                        element_type,
                                                        name_span,
                                                        section_name,
                                                        section_capacity,
                                                        section_name_length,
                                                        has_section,
                                                        explicit_alignment);
            }
            return parse_static_inferred_integer_array(parser,
                                                       element_type,
                                                       name_span,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment);
        }
    }
    {
        bool is_array;

        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &object_type, &is_array) ||
'''
new = '''    {
        MinicParser probe;
        bool incomplete_multidimensional;

        incomplete_multidimensional = false;
        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
            incomplete_multidimensional = probe.current.kind == MINIC_TOKEN_LBRACKET;
            if (!incomplete_multidimensional) {
                if (minic_type_is_char_integer(element_type)) {
                    return parse_static_inferred_char_array(parser,
                                                            element_type,
                                                            name_span,
                                                            section_name,
                                                            section_capacity,
                                                            section_name_length,
                                                            has_section,
                                                            explicit_alignment);
                }
                return parse_static_inferred_integer_array(parser,
                                                           element_type,
                                                           name_span,
                                                           section_name,
                                                           section_capacity,
                                                           section_name_length,
                                                           has_section,
                                                           explicit_alignment);
            }
        }
        {
            bool is_array;

            if (!minic_parser_parse_array_declarator_suffix(
                    parser, element_type, incomplete_multidimensional, &object_type, &is_array) ||
'''
if old not in text:
    raise SystemExit("inferred multidimensional static-array anchor not found")
text = text.replace(old, new, 1)

old_tail = '''            return false;
        }
    }
    if (existing_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
'''
new_tail = '''                return false;
            }
        }
    }
    if (existing_object_id != MINIC_GLOBAL_OBJECT_INVALID) {
'''
# Only replace the first tail after the newly inserted nested block.
anchor = text.index("incomplete_multidimensional, &object_type, &is_array)")
tail = text.find(old_tail, anchor)
if tail < 0:
    raise SystemExit("inferred multidimensional static-array tail anchor not found")
text = text[:tail] + text[tail:].replace(old_tail, new_tail, 1)

path.write_text(text)
