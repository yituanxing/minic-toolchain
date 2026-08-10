#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_global.c")
text = path.read_text()
old = r'''bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_name(parser, &object_type)) {
        return false;
    }
'''
new = r'''bool minic_parser_parse_extern_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType base_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;

    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_EXTERN, "expected keyword 'extern'") ||
        !minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &object_type)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"extern incomplete record: expected one staged extern-global prologue, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged extern object declarators without premature complete-type requirement; definitions remain strict")
