#!/usr/bin/env python3
from pathlib import Path


def function_region(text: str, signature: str) -> tuple[int, int, str]:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"extern incomplete record: missing {signature}")
    end_candidates = [
        pos
        for pos in (
            text.find("\nstatic ", start + len(signature)),
            text.find("\nbool ", start + len(signature)),
            text.find("\nvoid ", start + len(signature)),
        )
        if pos >= 0
    ]
    end = min(end_candidates) if end_candidates else len(text)
    return start, end, text[start:end]


# The active parser_global.c is already transformed by Lua discovery. Patch only
# the semantic seam inside parse_extern_global instead of matching its whole
# prologue, so later declaration metadata additions do not invalidate this step.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
start, end, body = function_region(text, "bool minic_parser_parse_extern_global(MinicParser *parser) {")

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

# Lua discovery classifies `extern T name...` with a non-consuming parser probe
# before selecting the function-vs-object parser. That classifier must also
# tolerate an incomplete record; the real function path remains strict, so an
# invalid function return type is still rejected when selected.
path = Path("src/frontend/parser_function.c")
text = path.read_text()
start, end, body = function_region(
    text, "static bool extern_declaration_is_function(MinicParser *parser, bool *is_function) {")
probe_decl = "    MinicType declared_type;\n"
if body.count(probe_decl) != 1:
    raise SystemExit(
        f"extern incomplete record: expected one probe declared type, found {body.count(probe_decl)}")
body = body.replace(probe_decl, "    MinicType base_type;\n    MinicType declared_type;\n", 1)
probe_strict = "!minic_parser_advance(&probe) || !minic_parser_parse_type_name(&probe, &declared_type)"
probe_relaxed = (
    "!minic_parser_advance(&probe) ||\n"
    "        !minic_parser_parse_type_specifiers(&probe, &base_type) ||\n"
    "        !minic_parser_parse_pointer_declarator(&probe, base_type, &declared_type)"
)
if body.count(probe_strict) != 1:
    raise SystemExit(
        f"extern incomplete record: expected one strict extern probe, found {body.count(probe_strict)}")
body = body.replace(probe_strict, probe_relaxed, 1)
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
