#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_record.c")
text = path.read_text()
old = r'''bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicType record_type;

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
'''
new = r'''bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicParser probe;
    MinicType record_type;
    bool is_forward_declaration;

    if (parser == NULL) {
        return false;
    }

    probe = *parser;
    is_forward_declaration = false;
    if (minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_IDENTIFIER &&
        minic_parser_advance(&probe) && probe.current.kind == MINIC_TOKEN_SEMICOLON) {
        is_forward_declaration = true;
    }

    if (is_forward_declaration) {
        return minic_parser_parse_type_specifiers(parser, &record_type) &&
               minic_type_is_record(record_type) &&
               minic_parser_expect(
                   parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record declaration");
    }

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
'''
if text.count(old) != 1:
    raise SystemExit(f"unexpected record-definition wrapper count={text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged struct/union tag forward declarations")
