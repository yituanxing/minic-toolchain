from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/parser_global.c"
text = path.read_text()
old = '''        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
'''
new = '''        uint64_t bits;

        if (!minic_parser_parse_integer_initializer_bits(parser, element_type, &bits) ||
            !minic_c0_global_object_add_initializer_bits(parser->program, object_id, bits)) {
'''
region_start = text.find("static bool parse_static_inferred_integer_array(")
region_end = text.find("static bool parse_static_inferred_char_array(", region_start)
if region_start < 0 or region_end < 0:
    raise SystemExit("cannot locate inferred static integer array helper")
region = text[region_start:region_end]
if old in region:
    if region.count(old) != 1:
        raise SystemExit(f"expected one legacy initializer anchor in helper, found {region.count(old)}")
    region = region.replace(old, new, 1)
    text = text[:region_start] + region + text[region_end:]
    path.write_text(text)
elif new not in region:
    raise SystemExit("typed initializer anchor not found")

region = path.read_text()[region_start:path.read_text().find("static bool parse_static_inferred_char_array(", region_start)]
if "minic_parser_parse_integer_value" in region or "minic_c0_global_object_add_initializer(parser" in region:
    raise SystemExit("inferred static integer array still uses legacy host-int initializer path")
if "minic_parser_parse_integer_initializer_bits" not in region or "minic_c0_global_object_add_initializer_bits" not in region:
    raise SystemExit("typed initializer owner is missing")
print("staged typed inferred static integer array initializers")
