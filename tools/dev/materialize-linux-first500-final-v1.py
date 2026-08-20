#!/usr/bin/env python3
"""Materialize the first validated Linux first500 convergence slice once."""
from pathlib import Path


def replace_once(path: Path, before: str, after: str) -> None:
    text = path.read_text()
    if after in text:
        return
    count = text.count(before)
    if count != 1:
        raise SystemExit(f"{path}: expected one materialization anchor, found {count}")
    path.write_text(text.replace(before, after, 1))


statement_path = Path("src/frontend/parser_statement.c")
replace_once(
    statement_path,
    "static bool parse_label(MinicParser *parser, bool allow_declaration) {\n",
    """static bool consume_gnu_label_attribute(MinicParser *parser,
                                        const MinicParsedAttribute *attribute,
                                        void *opaque_context) {
    const MinicAttributeDescriptor *descriptor;

    (void)opaque_context;
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_STATEMENT)) {
        minic_parser_error(parser, \"unsupported GNU label attribute\");
        return false;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_UNUSED &&
        descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL) {
        return true;
    }
    minic_parser_error(parser, \"GNU label attribute semantics are not implemented\");
    return false;
}

static bool parse_label(MinicParser *parser, bool allow_declaration) {
""",
)
replace_once(
    statement_path,
    """    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, \"expected ':' after label\")) {
        return false;
    }
""",
    """    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_COLON, \"expected ':' after label\") ||
        !minic_parser_parse_gnu_attribute_lists(parser, consume_gnu_label_attribute, NULL)) {
        return false;
    }
""",
)

global_path = Path("src/frontend/parser_global.c")
replace_once(
    global_path,
    """        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                true,
                                                minic_type_is_const(object_type),
                                                &object_id) ||
            !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
            (has_section && !minic_c0_global_object_set_section(
                                parser->program, object_id, section_name, section_name_length)) ||
            (explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                             parser->program, object_id, explicit_alignment))) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, \"cannot create static zero-definition declarator\");
            }
            return false;
        }
""",
    """        object_id = minic_parser_find_global_object_entity(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
            if (!minic_c0_program_add_tentative_global_object(
                    parser->program,
                    parser->source + name_span.begin.offset,
                    minic_parser_span_length(name_span),
                    object_type,
                    true,
                    static_object_type_is_read_only(parser->program, object_type),
                    &object_id)) {
                minic_parser_error(parser, \"cannot create static tentative declarator\");
                return false;
            }
        } else {
            const MinicGlobalObject *existing;

            existing = minic_c0_program_global_object(parser->program, object_id);
            if (existing == NULL || !existing->is_internal ||
                !minic_type_equal(existing->type, object_type) ||
                !minic_c0_global_object_merge_tentative(parser->program, object_id)) {
                minic_parser_error(parser, \"conflicting static tentative declarator\");
                return false;
            }
        }
        if ((has_section && !minic_c0_global_object_set_section(
                                parser->program, object_id, section_name, section_name_length)) ||
            (explicit_alignment != 0U && !minic_c0_global_object_set_explicit_alignment(
                                             parser->program, object_id, explicit_alignment))) {
            minic_parser_error(parser, \"cannot persist static tentative declarator metadata\");
            return false;
        }
""",
)
