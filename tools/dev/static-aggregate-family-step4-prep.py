from pathlib import Path

p = Path('src/frontend/parser_global.c')
text = p.read_text()
old = r'''    } else if (minic_type_is_pointer(type)) {
        uint64_t pointer_bits;

        if (!parse_static_pointer_constant_bits(parser, type, &pointer_bits) ||
            !minic_c0_global_object_add_initializer_bits(
                parser->program, object_id, pointer_bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static pointer constant bits");
            }
            return false;
        }
'''
new = r'''    } else if (minic_type_is_pointer(type)) {
        uint64_t pointer_bits;

        if (!parse_static_pointer_constant_bits(parser, type, &pointer_bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, pointer_bits)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record static pointer constant bits");
            }
            return false;
        }
'''
if text.count(old) != 1:
    raise SystemExit(f'step4 prep anchor mismatch: {text.count(old)}')
p.write_text(text.replace(old, new, 1))
