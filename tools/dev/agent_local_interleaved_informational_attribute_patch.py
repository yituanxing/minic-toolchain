from pathlib import Path

root = Path(__file__).resolve().parents[2]


def replace_once(path: Path, old: str, new: str, label: str) -> None:
    text = path.read_text()
    if old in text:
        if text.count(old) != 1:
            raise SystemExit(f"{label}: expected one old anchor, found {text.count(old)}")
        path.write_text(text.replace(old, new, 1))
        return
    if new in text:
        return
    raise SystemExit(f"{label}: neither old nor new anchor found")


parser = root / "src/frontend/parser_statement.c"
old = '''static bool parse_declaration(MinicParser *parser) {\n    MinicType base_type;\n    bool is_register_storage;\n\n    if (!minic_parser_parse_local_storage_class(parser, &is_register_storage) ||\n        !minic_parser_parse_type_specifiers(parser, &base_type)) {\n        return false;\n    }\n\n    for (;;) {\n'''
new = '''static bool parse_local_declaration_head_attributes(MinicParser *parser) {\n    return minic_parser_parse_gnu_attribute_lists(parser, consume_local_object_attribute, NULL);\n}\n\nstatic bool parse_declaration(MinicParser *parser) {\n    MinicType base_type;\n    bool is_register_storage;\n\n    if (!minic_parser_parse_local_storage_class(parser, &is_register_storage) ||\n        !minic_parser_parse_type_specifiers(parser, &base_type) ||\n        !parse_local_declaration_head_attributes(parser)) {\n        return false;\n    }\n\n    for (;;) {\n'''
replace_once(parser, old, new, "local declaration-head attribute seam")

focused = root / "tests/compiler/c0/run-foundation-focused.sh"
replace_once(
    focused,
    "    run-gnu-auto-type-local.sh \\\n",
    "    run-gnu-auto-type-local.sh \\\n    run-gnu-local-interleaved-informational-attribute.sh \\\n",
    "Foundation local interleaved attribute gate",
)

text = parser.read_text()
region = text.split("static bool parse_local_declaration_head_attributes", 1)[1].split(
    "static bool parse_declaration", 1
)[0]
if "consume_local_object_attribute, NULL" not in region:
    raise SystemExit("declaration-head attributes are not using the canonical local object consumer")
print("staged local declaration-head informational attribute seam")
