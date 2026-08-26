#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_statement.c")
text = path.read_text()

marker = "M175C_LOCAL_COMPLETE_OBJECT_OWNER"
if marker in text:
    print("M175C local complete-object owner already present")
    raise SystemExit(0)

anchor = '''    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    if (!parse_local_fixed_register_name(parser,
'''
replacement = '''    if (minic_type_is_void(declared_type)) {
        minic_parser_error(parser, "local object cannot have void type");
        return false;
    }
    /* M175C_LOCAL_COMPLETE_OBJECT_OWNER: a local declaration owns storage, so
       record/enum object types must be complete here. Pointer declarators remain
       valid because the shared complete-object query accepts pointer types. */
    if (!minic_parser_require_complete_object_type(
            parser, declared_type, "local object type is incomplete")) {
        return false;
    }
    if (!parse_local_fixed_register_name(parser,
'''

if text.count(anchor) != 1:
    raise SystemExit(
        f"M175C local complete-object owner: expected one declarator anchor, found {text.count(anchor)}"
    )

path.write_text(text.replace(anchor, replacement, 1))
print("staged M175C local complete-object declaration owner")
