#!/usr/bin/env python3
from pathlib import Path


# The active parser_global.c is already transformed by Lua discovery. Patch only
# the semantic seam inside parse_extern_global instead of matching its whole
# prologue, so later declaration metadata additions do not invalidate this step.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
start = text.find("bool minic_parser_parse_extern_global(MinicParser *parser) {")
if start < 0:
    raise SystemExit("extern incomplete record: missing parse_extern_global")
end_candidates = [
    pos
    for pos in (
        text.find("\nstatic ", start + 1),
        text.find("\nbool ", start + 1),
    )
    if pos >= 0
]
end = min(end_candidates) if end_candidates else len(text)
body = text[start:end]

decl = "    MinicType object_type;\n"
if body.count(decl) != 1:
    raise SystemExit(f"extern incomplete record: expected one object type declaration, found {body.count(decl)}")
body = body.replace(decl, "    MinicType base_type;\n    MinicType object_type;\n", 1)

strict = "!minic_parser_parse_type_name(parser, &object_type)"
relaxed = "!minic_parser_parse_type_specifiers(parser, &base_type) ||\n        !minic_parser_parse_pointer_declarator(parser, base_type, &object_type)"
if body.count(strict) != 1:
    raise SystemExit(f"extern incomplete record: expected one strict type-name call, found {body.count(strict)}")
body = body.replace(strict, relaxed, 1)
path.write_text(text[:start] + body + text[end:])

# A pure extern declaration owns no storage in this translation unit. Mirror the
# existing incomplete-extern-array treatment: incomplete record declarations
# get zero layout metadata and are skipped; complete records still use normal
# target layout. Definitions remain on the strict parser path and cannot reach
# this exception.
path = Path("src/target/riscv64/layout.c")
text = path.read_text()
anchor = """        object = &program->global_objects[object_index];
        if (object->is_extern && minic_type_is_array(object->type)) {
"""
replacement = """        object = &program->global_objects[object_index];
        if (object->is_extern && minic_type_is_record(object->type)) {
            const MinicRecord *record;

            record = minic_c0_program_record(program, object->type.record_id);
            if (record != NULL && !record->is_complete) {
                object->storage_size = 0U;
                object->alignment = 0U;
                continue;
            }
        }
        if (object->is_extern && minic_type_is_array(object->type)) {
"""
if text.count(anchor) != 1:
    raise SystemExit(f"extern incomplete record: expected incomplete-array layout seam once, found {text.count(anchor)}")
path.write_text(text.replace(anchor, replacement, 1))

print("staged extern incomplete record object declarations without forcing target layout")
