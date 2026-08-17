#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_function.c"
text = path.read_text()

parse_anchor = '''bool minic_parser_parse_parameter_list(MinicParser *parser,\n                                       MinicSourceSpan *parameter_name_spans,\n                                       MinicType *parameter_types,\n                                       size_t *parameter_count,\n                                       bool require_names,\n                                       bool *is_variadic) {\n'''
helper = '''static bool parameter_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {\n    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||\n           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||\n           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||\n           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;\n}\n\nstatic bool consume_parameter_declarator_attribute(MinicParser *parser,\n                                                   const MinicParsedAttribute *attribute,\n                                                   void *opaque_context) {\n    const MinicAttributeDescriptor *descriptor;\n\n    (void)opaque_context;\n    if (parser == NULL || attribute == NULL) {\n        return false;\n    }\n    descriptor = attribute->descriptor;\n    if (descriptor == NULL ||\n        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {\n        minic_parser_error(parser, \"unsupported GNU parameter declarator attribute\");\n        return false;\n    }\n    if (parameter_attribute_class_is_parse_only(descriptor->semantic_class)) {\n        return true;\n    }\n    minic_parser_error(\n        parser,\n        \"GNU parameter declarator attribute requires explicit language/layout semantics\");\n    return false;\n}\n\nbool minic_parser_parse_parameter_list(MinicParser *parser,\n                                       MinicSourceSpan *parameter_name_spans,\n                                       MinicType *parameter_types,\n                                       size_t *parameter_count,\n                                       bool require_names,\n                                       bool *is_variadic) {\n'''
if text.count(parse_anchor) != 1:
    raise SystemExit(f"parameter list anchor count={text.count(parse_anchor)}")
text = text.replace(parse_anchor, helper, 1)

adjust_anchor = '''        if (!is_function_pointer_parameter &&\n            !adjust_array_parameter_type(parser, &parameter_type)) {\n            return false;\n        }\n'''
adjust_replacement = '''        if (!minic_parser_parse_gnu_attribute_lists(\n                parser, consume_parameter_declarator_attribute, NULL)) {\n            return false;\n        }\n\n        if (!is_function_pointer_parameter &&\n            !adjust_array_parameter_type(parser, &parameter_type)) {\n            return false;\n        }\n'''
if text.count(adjust_anchor) != 1:
    raise SystemExit(f"parameter adjustment anchor count={text.count(adjust_anchor)}")
text = text.replace(adjust_anchor, adjust_replacement, 1)

path.write_text(text)
