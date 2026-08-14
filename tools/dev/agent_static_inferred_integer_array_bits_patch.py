from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()
old = '''    initializer_count = 0U;\n    while (parser->current.kind != MINIC_TOKEN_RBRACE) {\n        int value;\n\n        if (!minic_parser_parse_integer_value(parser, &value) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\\\0') {\n                minic_parser_error(parser, "cannot record inferred static integer initializer");\n            }\n            return false;\n        }\n'''
new = '''    initializer_count = 0U;\n    while (parser->current.kind != MINIC_TOKEN_RBRACE) {\n        uint64_t bits;\n\n        if (!minic_parser_parse_integer_initializer_bits(parser, element_type, &bits) ||\n            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {\n            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\\\\0') {\n                minic_parser_error(parser, "cannot record inferred static integer initializer");\n            }\n            return false;\n        }\n'''
if old in text:
    if text.count(old) != 1:
        raise SystemExit(f"expected one legacy initializer anchor, found {text.count(old)}")
    path.write_text(text.replace(old, new, 1))
elif new not in text:
    raise SystemExit("typed initializer anchor not found")

region = path.read_text().split("static bool parse_static_inferred_integer_array(", 1)[1].split(
    "static bool parse_static_inferred_char_array(", 1
)[0]
if "minic_parser_parse_integer_value" in region or "minic_c0_global_object_add_initializer(parser" in region:
    raise SystemExit("inferred static integer array still uses legacy host-int initializer path")
if "minic_parser_parse_integer_initializer_bits" not in region or "minic_c0_global_object_add_initializer_bits" not in region:
    raise SystemExit("typed initializer owner is missing")
print("staged typed inferred static integer array initializers")
