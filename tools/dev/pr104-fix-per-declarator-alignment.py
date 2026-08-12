#!/usr/bin/env python3
from pathlib import Path

p = Path("src/frontend/parser_global.c")
text = p.read_text()
start = text.find("bool minic_parser_parse_extern_global_after_head(")
end = text.find("bool minic_parser_parse_extern_global(MinicParser *parser)", start)
if start < 0 or end < 0:
    raise SystemExit("extern after-head boundaries missing")
chunk = text[start:end]

# The first materializer intentionally rewires merge consumers but its textual
# replacement also catches the parameter spelling. Restore a distinct shared
# declaration-head value and keep each loop iteration's suffix state isolated.
old_sig = "                                                 size_t declarator_explicit_alignment,\n"
new_sig = "                                                 size_t shared_explicit_alignment,\n"
if chunk.count(old_sig) != 1:
    raise SystemExit(f"rewritten alignment parameter count={chunk.count(old_sig)}")
chunk = chunk.replace(old_sig, new_sig, 1)

old_init = "        declarator_explicit_alignment = explicit_alignment;\n"
new_init = "        declarator_explicit_alignment = shared_explicit_alignment;\n"
if chunk.count(old_init) != 1:
    raise SystemExit(f"per-declarator alignment initializer count={chunk.count(old_init)}")
chunk = chunk.replace(old_init, new_init, 1)

old_condition = "                   (explicit_alignment != 0U &&\n"
new_condition = "                   (declarator_explicit_alignment != 0U &&\n"
if chunk.count(old_condition) != 1:
    raise SystemExit(f"new-object alignment condition count={chunk.count(old_condition)}")
chunk = chunk.replace(old_condition, new_condition, 1)

old_set = "                        parser->program, object_id, explicit_alignment)) ||\n"
new_set = "                        parser->program, object_id, declarator_explicit_alignment)) ||\n"
if chunk.count(old_set) != 1:
    raise SystemExit(f"new-object alignment setter count={chunk.count(old_set)}")
chunk = chunk.replace(old_set, new_set, 1)

p.write_text(text[:start] + chunk + text[end:])
