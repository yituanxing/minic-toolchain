from pathlib import Path

path = Path("tools/dev/pr128-materialize.py")
text = path.read_text()
old = '''# Two object branches have the same first apply block; replace both deliberately.
if text.count(old) != 2:
    raise SystemExit(f"object attribute routing anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 2)
func.write_text(text)
'''
new = '''# The static branch has the `has_visibility` tail used by the anchor above.
if text.count(old) != 1:
    raise SystemExit(f"static object attribute routing anchor mismatch: {text.count(old)}")
text = text.replace(old, new, 1)
external_old = '''        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (is_extern_declaration) {
'''
external_new = '''        if (!minic_parser_apply_object_attribute_list(parser,
                                                      &deferred_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment) ||
            !minic_parser_apply_object_attribute_list(parser,
                                                      &declarator_attributes,
                                                      section_name,
                                                      sizeof(section_name),
                                                      &section_name_length,
                                                      &has_section,
                                                      &object_explicit_alignment)) {
            return false;
        }
        if (is_extern_declaration) {
'''
if text.count(external_old) != 1:
    raise SystemExit(f"external object attribute routing anchor mismatch: {text.count(external_old)}")
text = text.replace(external_old, external_new, 1)
func.write_text(text)
'''
if text.count(old) != 1:
    raise SystemExit(f"materializer repair anchor mismatch: {text.count(old)}")
path.write_text(text.replace(old, new, 1))
