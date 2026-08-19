from pathlib import Path

path = Path(__file__).resolve().parents[2] / "src/frontend/parser_global.c"
text = path.read_text()
anchor = '''    final_capacity = 0U;
    extent = 0U;
    success = false;
    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
'''
replacement = '''    final_capacity = 0U;
    extent = 0U;
    success = false;
    if (parser != NULL && !infer_bound && element_count != 0U &&
        minic_type_is_char_integer(element_type) &&
        parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        return minic_parser_add_bounded_string_literal_initializer(
            parser, object_id, element_count);
    }

    minic_array_initializer_plan_initialize(&plan, element_count, infer_bound);
'''
if anchor not in text:
    raise SystemExit("static scalar array transaction anchor missing")
path.write_text(text.replace(anchor, replacement, 1))
print("ported character array string initializer into static scalar array owner")
