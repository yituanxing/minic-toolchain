#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()

old_sig = """static bool
parse_static_pointer_array(MinicParser *parser, MinicType element_type, MinicSourceSpan name_span) {
"""
new_sig = """static bool parse_static_pointer_array(MinicParser *parser,
                                       MinicType element_type,
                                       MinicSourceSpan name_span,
                                       char *section_name,
                                       size_t section_capacity,
                                       size_t *section_name_length,
                                       bool *has_section,
                                       size_t *explicit_alignment) {
"""
if old_sig in text:
    text = text.replace(old_sig, new_sig, 1)
elif new_sig not in text:
    raise SystemExit("unexpected static pointer array signature")

attr_old = """    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{'")) {
"""
attr_new = """    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "multi-dimensional static pointer arrays are not supported yet");
        goto done;
    }
    if (!minic_parser_parse_gnu_object_attribute_lists(parser,
                                                       section_name,
                                                       section_capacity,
                                                       section_name_length,
                                                       has_section,
                                                       explicit_alignment) ||
        !minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{'")) {
"""
if attr_old in text:
    text = text.replace(attr_old, attr_new, 1)
elif attr_new not in text:
    raise SystemExit("unexpected post-array attribute anchor")

init_old = """        if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            MinicType literal_type;
            MinicSourceSpan literal_span;

            if (!minic_type_assignment_compatible(element_type, string_pointer_type) ||
                !minic_parser_create_string_literal_object(
                    parser, &target_id, &literal_type, &literal_span)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser, "string literal does not match static pointer array element");
                }
                goto done;
            }
            (void)literal_type;
            (void)literal_span;
        } else if (!minic_parser_parse_null_pointer_constant_expression(parser, element_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "static pointer array scalar initializer must be null");
            }
            goto done;
        }
"""
init_new = """        if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            MinicType literal_type;
            MinicSourceSpan literal_span;

            if (!minic_type_assignment_compatible(element_type, string_pointer_type) ||
                !minic_parser_create_string_literal_object(
                    parser, &target_id, &literal_type, &literal_span)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser, "string literal does not match static pointer array element");
                }
                goto done;
            }
            (void)literal_type;
            (void)literal_span;
        } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            MinicExpressionId value_id;

            if (!minic_parser_parse_expression(parser, &value_id, 0U) ||
                !minic_c0_assignment_compatible(parser->program, element_type, value_id) ||
                !static_object_address_relocation_target(parser->program, value_id, &target_id)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                    minic_parser_error(
                        parser,
                        "static pointer array object initializer requires a compatible zero-addend object address");
                }
                goto done;
            }
        } else if (!minic_parser_parse_null_pointer_constant_expression(parser, element_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\0') {
                minic_parser_error(parser, "static pointer array scalar initializer must be null");
            }
            goto done;
        }
"""
if init_old in text:
    text = text.replace(init_old, init_new, 1)
elif init_new not in text:
    raise SystemExit("unexpected pointer initializer anchor")

object_old = """        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
"""
object_new = """        !minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            minic_type_is_const(element_type),
                                            &object_id) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        (*has_section &&
         !minic_c0_global_object_set_section(
             parser->program, object_id, section_name, *section_name_length)) ||
        (*explicit_alignment != 0U &&
         !minic_c0_global_object_set_explicit_alignment(
             parser->program, object_id, *explicit_alignment))) {
"""
if object_old in text:
    text = text.replace(object_old, object_new, 1)
elif object_new not in text:
    raise SystemExit("unexpected pointer array object metadata anchor")

call_old = """    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser, element_type, name_span);
    }
"""
call_new = """    if (minic_type_is_pointer(element_type)) {
        return parse_static_pointer_array(parser,
                                          element_type,
                                          name_span,
                                          section_name,
                                          section_capacity,
                                          section_name_length,
                                          has_section,
                                          explicit_alignment);
    }
"""
if call_old in text:
    text = text.replace(call_old, call_new, 1)
elif call_new not in text:
    raise SystemExit("unexpected static pointer array call anchor")

path.write_text(text)

fixture = Path("tests/compiler/c0/static_pointer_array.c")
source = fixture.read_text()
if "static int *levels[]" not in source:
    source = """extern int start_a[];
extern int start_b[];

static int *levels[] __attribute__((section(".init.data"))) = {start_a, start_b};
""" + source
    fixture.write_text(source)

run = Path("tests/compiler/c0/run-static-pointer-arrays.sh")
script = run.read_text()
if "levels:" not in script:
    anchor = "grep -F 'names:' \"$work/static_pointer_array.s\" >/dev/null\n"
    insert = """grep -F '.section .init.data' "$work/static_pointer_array.s" >/dev/null
grep -F 'levels:' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword start_a' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword start_b' "$work/static_pointer_array.s" >/dev/null
grep -F '.size levels, 16' "$work/static_pointer_array.s" >/dev/null
"""
    if script.count(anchor) != 1:
        raise SystemExit("unexpected static pointer-array gate anchor")
    script = script.replace(anchor, insert + anchor, 1)
    script = script.replace(
        "inferred-bound=3 object-relocations=2 null-tail=1'",
        "inferred-bound=3 object-relocations=2 null-tail=1 suffix-section=1 extern-array-decay=2'",
    )
    run.write_text(script)
