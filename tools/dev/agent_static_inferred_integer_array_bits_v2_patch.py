from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()
start = text.find("static bool parse_static_inferred_integer_array(")
end = text.find("static bool parse_static_inferred_char_array(", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate inferred static integer array helper")
region = text[start:end]
old = '''        int value;\n\n        if (!minic_parser_parse_integer_value(parser, &value) ||\n            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {\n'''
new = '''        uint64_t bits;\n\n        if (!minic_parser_parse_integer_initializer_bits(parser, element_type, &bits) ||\n            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {\n'''
if old in region:
    if region.count(old) != 1:
        raise SystemExit(f"expected one legacy initializer anchor, found {region.count(old)}")
    region = region.replace(old, new, 1)
    path.write_text(text[:start] + region + text[end:])
elif new not in region:
    raise SystemExit("typed initializer anchor not found")

region = path.read_text()[start:path.read_text().find("static bool parse_static_inferred_char_array(", start)]
if "minic_parser_parse_integer_value" in region or "minic_c0_global_object_add_initializer(parser" in region:
    raise SystemExit("legacy host-int initializer path remains")
print("staged canonical typed inferred static integer array bits")
