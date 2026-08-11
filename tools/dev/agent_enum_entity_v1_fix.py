#!/usr/bin/env python3
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]

# This runs after agent_enum_entity_v1_patch.py and only refines the generated slice.

p = ROOT / "src/frontend/parser_enum.c"
s = p.read_text()
s, count = re.subn(
    r'''static MinicParserEnumTag \*find_enum_tag_binding\(MinicParser \*parser, MinicSourceSpan name_span\) \{.*?\n\}\n\n''',
    "",
    s,
    count=1,
    flags=re.S,
)
if count != 1:
    raise SystemExit(f"remove unused enum tag helper: expected 1, found {count}")
p.write_text(s)

p = ROOT / "src/frontend/parser_function.c"
s = p.read_text()
anchor = '''bool minic_parser_parse_parameter_list(MinicParser *parser,
                                       MinicSourceSpan *parameter_name_spans,
                                       MinicType *parameter_types,
                                       size_t *parameter_count,
                                       bool require_names,
                                       bool *is_variadic) {
'''
helper = '''static bool parse_function_signature_type_name(MinicParser *parser, MinicType *type) {
    MinicType base_type;

    if (parser == NULL || type == NULL ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, type)) {
        return false;
    }
    if (minic_type_is_record(*type)) {
        return minic_parser_require_complete_object_type(
            parser, *type, "incomplete record type requires pointer declarator");
    }
    return true;
}

'''
if s.count(anchor) != 1:
    raise SystemExit("parameter-list anchor mismatch")
s = s.replace(anchor, helper + anchor, 1)
old = '''        if (!minic_parser_parse_type_name(parser, &parameter_type)) {
            return false;
        }
'''
if s.count(old) != 1:
    raise SystemExit(f"parameter type parser: expected 1, found {s.count(old)}")
s = s.replace(old, '''        if (!parse_function_signature_type_name(parser, &parameter_type)) {
            return false;
        }
''', 1)
old = '''    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly") ||
        !minic_parser_require_complete_object_type(
            parser, return_type, "incomplete record type requires pointer declarator")) {
        return false;
    }
'''
new = '''    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly") ||
        (minic_type_is_record(return_type) &&
         !minic_parser_require_complete_object_type(
             parser, return_type, "incomplete record type requires pointer declarator"))) {
        return false;
    }
'''
if s.count(old) != 1:
    raise SystemExit("function return declaration completion anchor mismatch")
s = s.replace(old, new, 1)
old = '''    if (minic_type_is_record(return_type) &&
        !minic_parser_require_complete_object_type(
            parser, return_type, "function definition requires a complete record return type")) {
        return false;
    }
'''
new = '''    if ((minic_type_is_record(return_type) || minic_type_is_enum(return_type)) &&
        !minic_parser_require_complete_object_type(
            parser, return_type, "function definition requires a complete return type")) {
        return false;
    }
'''
if s.count(old) != 1:
    raise SystemExit("function definition return completion anchor mismatch")
s = s.replace(old, new, 1)
# Before entering a function body, by-value enum parameters must be complete because they become locals.
anchor = '''    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected ';' or '{' after function declarator");
        return false;
    }
    {
        size_t parameter_index;
'''
new = '''    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected ';' or '{' after function declarator");
        return false;
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            if (minic_type_is_enum(parameter_types[parameter_index]) &&
                !minic_parser_require_complete_object_type(
                    parser,
                    parameter_types[parameter_index],
                    "function definition requires complete enum parameter types")) {
                return false;
            }
        }
    }
    {
        size_t parameter_index;
'''
if s.count(anchor) != 1:
    raise SystemExit("function body parameter completion anchor mismatch")
s = s.replace(anchor, new, 1)
p.write_text(s)
