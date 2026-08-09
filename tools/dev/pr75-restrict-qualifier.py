#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_type.c")
text = path.read_text()
old = '''        while (parser->current.kind == MINIC_TOKEN_KW_CONST) {
            if (!minic_type_add_const(parsed_type, &parsed_type)) {
                minic_parser_error(parser, "cannot apply pointer const qualifier");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
'''
new = '''        while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
               minic_parser_identifier_is(parser, "restrict") ||
               minic_parser_identifier_is(parser, "__restrict")) {
            if (parser->current.kind == MINIC_TOKEN_KW_CONST &&
                !minic_type_add_const(parsed_type, &parsed_type)) {
                minic_parser_error(parser, "cannot apply pointer const qualifier");
                return false;
            }
            /* restrict is an aliasing promise, not an ABI/layout qualifier. MiniC does
               not yet perform restrict-based alias optimization, so accepting it here
               preserves observable semantics while keeping the target type unchanged. */
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
'''
if text.count(old) != 1:
    raise SystemExit(f"pointer qualifier loop: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged C restrict and GNU __restrict pointer qualifiers")
