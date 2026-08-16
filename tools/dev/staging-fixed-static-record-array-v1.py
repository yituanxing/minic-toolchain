from pathlib import Path


def once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

p = Path('src/frontend/parser_global.c')
t = p.read_text()

t = once(
    t,
'''    if (minic_type_is_record(element_type)) {
        return parse_static_record(parser, element_type, name_span);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {''',
'''    if (minic_type_is_record(element_type) && parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return parse_static_record(parser, element_type, name_span);
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACKET) {''',
    'defer record array routing')

t = once(
    t,
'''    if (!minic_type_is_integer(element_type)) {
        minic_parser_error(parser,
                           "static array requires an integer, pointer, or record element type");
        return false;
    }
    {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_char_integer(element_type)) {''',
'''    if (!minic_type_is_integer(element_type) && !minic_type_is_record(element_type)) {
        minic_parser_error(parser,
                           "static array requires an integer, pointer, or record element type");
        return false;
    }
    {
        MinicParser probe;

        probe = *parser;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        if (probe.current.kind == MINIC_TOKEN_RBRACKET) {
            if (minic_type_is_record(element_type)) {
                return parse_static_record(parser, element_type, name_span);
            }
            if (minic_type_is_char_integer(element_type)) {''',
    'keep inferred record owner')

t = once(
    t,
'''        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &object_type, &is_array) ||
            !is_array) {''',
'''        if (!minic_parser_parse_array_declarator_suffix(
                parser, element_type, false, &object_type, &is_array) ||
            !is_array ||
            !minic_parser_parse_gnu_object_attribute_lists(parser,
                                                           section_name,
                                                           section_capacity,
                                                           section_name_length,
                                                           has_section,
                                                           explicit_alignment)) {''',
    'fixed array suffix attributes')

p.write_text(t)

p = Path('tests/compiler/c0/run-foundation-focused.sh')
t = p.read_text()
anchor = '    run-static-preformed-array-zero.sh \\\n'
if anchor not in t:
    raise SystemExit('foundation static-preformed anchor missing')
t = t.replace(anchor, anchor + '    run-static-fixed-record-array-zero.sh \\\n', 1)
p.write_text(t)
