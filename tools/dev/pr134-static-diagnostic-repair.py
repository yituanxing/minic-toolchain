from pathlib import Path

path = Path('src/frontend/parser_global.c')
text = path.read_text()
old = '''    if (minic_type_is_integer(type)) {
        int value;

        if (!minic_parser_parse_integer_initializer_value(parser, type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
'''
new = '''    if (minic_type_is_integer(type)) {
        int value;

        if (parser->current.kind == MINIC_TOKEN_LBRACE) {
            minic_parser_error(parser, "expected integer constant expression");
            return false;
        }
        if (!minic_parser_parse_integer_initializer_value(parser, type, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
'''
if text.count(old) != 1:
    raise SystemExit(f'static scalar typed-diagnostic anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
