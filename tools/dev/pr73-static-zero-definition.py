#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


path = Path("src/frontend/parser_global.c")
text = path.read_text()
marker = "bool minic_parser_parse_static_global(MinicParser *parser) {\n"
if text.count(marker) != 1:
    raise SystemExit("parser_global.c: static-global marker mismatch")
helper = r'''static bool parse_static_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type))) {
        return false;
    }
    if (minic_type_is_record(object_type) &&
        !minic_parser_require_complete_object_type(
            parser, object_type, "static object requires a complete record type")) {
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(object_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        minic_parser_error(parser, "cannot create zero-initialized static object");
        return false;
    }
    return minic_parser_advance(parser);
}

'''
path.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/parser_global.c",
    '''    if (minic_type_is_record(element_type)) {\n        return parse_static_record(parser, element_type, name_span);\n    }\n    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {\n''',
    '''    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {\n        return parse_static_zero_definition(parser, element_type, name_span);\n    }\n    if (minic_type_is_record(element_type)) {\n        return parse_static_record(parser, element_type, name_span);\n    }\n    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {\n''',
)

print("staged implicit zero initialization for static object definitions")
