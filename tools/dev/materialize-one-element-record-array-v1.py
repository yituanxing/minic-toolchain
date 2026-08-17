from pathlib import Path

path = Path('src/frontend/parser_global.c')
text = path.read_text()
old = '''            if (field->element_count == 1U) {
                if (!minic_parser_parse_static_storage_initializer_value(
                        parser, object_id, field->type)) {
                    return false;
                }
            } else if (minic_type_is_char_integer(field->type) &&
                       parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {'''
new = '''            if (field->element_count == 1U && !field->is_array) {
                if (!minic_parser_parse_static_storage_initializer_value(
                        parser, object_id, field->type)) {
                    return false;
                }
            } else if (field->is_array && minic_type_is_char_integer(field->type) &&
                       parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {'''
if text.count(old) != 1:
    raise SystemExit(f'one-element record-array anchor mismatch: {text.count(old)}')
path.write_text(text.replace(old, new, 1))
