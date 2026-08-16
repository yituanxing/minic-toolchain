from pathlib import Path


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

p = Path('src/frontend/parser_global.c')
t = p.read_text()

insert_before = '''static bool parse_static_zero_definition(MinicParser *parser,
                                         MinicType object_type,
                                         MinicSourceSpan name_span) {'''
helper = '''static bool static_object_type_is_read_only(const MinicC0Program *program, MinicType type) {
    const MinicArrayType *array_type;

    if (!minic_type_is_array(type)) {
        return minic_type_is_const(type);
    }
    array_type = minic_c0_program_array_type(program, type.array_type_id);
    return array_type != NULL && static_object_type_is_read_only(program, array_type->element_type);
}

''' + insert_before
t = once(t, insert_before, helper, 'read-only helper')

t = once(
    t,
'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type))) {
        return false;
    }
    if (minic_type_is_record(object_type) &&
        !minic_parser_require_complete_object_type(
            parser, object_type, "static object requires a complete record type")) {
        return false;
    }''',
'''    if (parser == NULL || parser->current.kind != MINIC_TOKEN_SEMICOLON ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type) &&
         !minic_type_is_record(object_type) && !minic_type_is_array(object_type))) {
        return false;
    }
    if (!minic_c0_type_is_complete_object(parser->program, object_type)) {
        minic_parser_error(parser, "static object requires a complete object type");
        return false;
    }''',
    'zero-definition completeness')

t = once(
    t,
'''                                            object_type,
                                            true,
                                            minic_type_is_const(object_type),
                                            &object_id) ||''',
'''                                            object_type,
                                            true,
                                            static_object_type_is_read_only(parser->program,
                                                                            object_type),
                                            &object_id) ||''',
    'zero-definition readonly')

t = once(
    t,
'''    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
         !minic_type_is_record(element_type))) {''',
'''    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type) &&
         !minic_type_is_record(element_type) && !minic_type_is_array(element_type))) {''',
    'static global type gate')

needle = '''    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_static_zero_definition(parser, element_type, name_span);
    }
    if (minic_type_is_record(element_type)) {'''
replacement = '''    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        return parse_static_zero_definition(parser, element_type, name_span);
    }
    if (minic_type_is_array(element_type)) {
        minic_parser_error(parser,
                           "pre-formed static array initializer is not supported yet");
        return false;
    }
    if (minic_type_is_record(element_type)) {'''
t = once(t, needle, replacement, 'preformed array routing')
p.write_text(t)

p = Path('tests/compiler/c0/run-foundation-focused.sh')
t = p.read_text()
anchor = '    run-static-mutable-arrays.sh \\\n'
if anchor not in t:
    raise SystemExit('foundation runner anchor missing')
t = t.replace(anchor, anchor + '    run-static-preformed-array-zero.sh \\\n', 1)
p.write_text(t)
