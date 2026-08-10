#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_statement.c"
text = path.read_text()

anchor = '''static bool\nparse_local_declarator(MinicParser *parser, MinicType base_type, bool is_register_storage) {\n'''
helper = '''static bool consume_local_object_attribute(MinicParser *parser,\n                                           const MinicParsedAttribute *attribute,\n                                           void *context) {\n    const MinicAttributeDescriptor *descriptor;\n\n    (void)context;\n    if (parser == NULL || attribute == NULL) {\n        return false;\n    }\n    descriptor = attribute->descriptor;\n    if (descriptor == NULL) {\n        minic_parser_error(parser, "unsupported GNU attribute on local object");\n        return false;\n    }\n    if (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {\n        minic_parser_error(parser, "GNU attribute is not valid on a local object");\n        return false;\n    }\n    if (attribute->has_arguments ||\n        descriptor->semantic_class != MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {\n        minic_parser_error(parser, "local object attribute semantics are not supported yet");\n        return false;\n    }\n    return true;\n}\n\nstatic bool parse_local_object_attributes(MinicParser *parser) {\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_local_object_attribute, NULL);\n}\n\nstatic bool\nparse_local_declarator(MinicParser *parser, MinicType base_type, bool is_register_storage) {\n'''
text = replace_once(text, anchor, helper, "local-object-attribute-helper")

old = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        if (!minic_parser_advance(parser) ||\n            !minic_parser_parse_fixed_array_bound(parser, &local.element_count)) {\n            return false;\n        }\n        local.is_array = true;\n    }\n    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {\n'''
new = '''    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {\n        if (!minic_parser_advance(parser) ||\n            !minic_parser_parse_fixed_array_bound(parser, &local.element_count)) {\n            return false;\n        }\n        local.is_array = true;\n    }\n    if (!parse_local_object_attributes(parser)) {\n        return false;\n    }\n    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {\n'''
text = replace_once(text, old, new, "local-object-attribute-attachment")
path.write_text(text)

path = root / "tests/compiler/c0/record_local_initializer.c"
text = path.read_text()
text = replace_once(
    text,
    '''static int initialize_guard(void) {\n    struct Guard guard = { .lock = (void *)1, .flags = 7 };\n    return (unsigned long)guard.lock == 1 && guard.flags == 7;\n}\n''',
    '''static int initialize_guard(void) {\n    struct Guard guard = { .lock = (void *)1, .flags = 7 },\n                 *guard_ptr __attribute__((__unused__)) = &guard;\n    return (unsigned long)guard.lock == 1 && guard.flags == 7 && guard_ptr == &guard;\n}\n''',
    "record-local-suffix-attribute-fixture",
)
path.write_text(text)

path = root / "tests/compiler/c0/run-record-local-initializers.sh"
text = path.read_text()
text = replace_once(
    text,
    "designated=pointer+integer zero-fill=unspecified member-selector=shared",
    "designated=pointer+integer zero-fill=unspecified member-selector=shared multi-declarator=1 suffix-object-attribute=unused",
    "record-local-attribute-summary",
)
path.write_text(text)
