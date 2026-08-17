#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
path = root / "src/frontend/ast_verifier.c"
text = path.read_text()

start_marker = "bool minic_c0_program_verify_target(const MinicC0Program *program,"
start = text.find(start_marker)
if start < 0:
    raise SystemExit("program verifier entry not found")
prefix = text[:start]
body = text[start:]

anchors = [
    ("    for (index = 0U; index < program->enum_count; ++index) {\n", "enum"),
    ("    for (index = 0U; index < program->enumerator_count; ++index) {\n", "enumerator"),
    ("    for (index = 0U; index < program->array_type_count; ++index) {\n", "array_type"),
    ("    for (index = 0U; index < program->function_type_count; ++index) {\n", "function_type"),
    ("    for (index = 0U; index < program->record_count; ++index) {\n", "record"),
    ("    for (index = 0U; index < program->local_count; ++index) {\n", "local"),
    ("    for (index = 0U; index < program->fixed_register_binding_count; ++index) {\n", "fixed_register"),
    ("    for (index = 0U; index < program->function_count; ++index) {\n", "function"),
    ("    for (index = 0U; index < program->type_alias_count; ++index) {\n", "type_alias"),
    ("    for (index = 0U; index < program->global_object_count; ++index) {\n", "global_object"),
    ("    for (index = 0U; index < program->file_asm_count; ++index) {\n", "file_asm"),
    ("    for (index = 0U; index < program->expression_count; ++index) {\n", "expression"),
    ("    for (index = 0U; index < program->statement_count; ++index) {\n", "statement"),
    ("    for (index = 0U; index < program->block_count; ++index) {\n", "block"),
]

for anchor, label in anchors:
    count = body.count(anchor)
    if count != 1:
        raise SystemExit(f"verifier stage anchor {label} count={count}")
    body = body.replace(
        anchor,
        f'    (void)fprintf(stderr, "VERIFY_STAGE {label}\\n");\n' + anchor,
        1,
    )

path.write_text(prefix + body)
