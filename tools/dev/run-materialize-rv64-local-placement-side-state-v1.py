#!/usr/bin/env python3
from pathlib import Path

path = Path("tools/dev/materialize-rv64-local-placement-side-state-v1.py")
source = path.read_text(encoding="utf-8")
old = '''start, end = function_span(text, "minic_riscv64_frame_layout")
if "minic_riscv64_frame_layout_from_function_layout" in text[start:end]:
    raise SystemExit("selected FrameLayout core instead of compatibility wrapper")
text = text[:start] + text[end:]
'''
new = '''start, end = function_span(text, "minic_riscv64_frame_layout")
text = text[:start] + text[end:]
'''
if source.count(old) != 1:
    raise SystemExit("FrameLayout wrapper staging guard anchor changed")
source = source.replace(old, new, 1)
exec(compile(source, str(path), "exec"), {"__name__": "__main__", "__file__": str(path)})

for frontend_path, line, expected_count in (
    ("src/frontend/parser_function.c", "            parameter_local.storage_offset = 0U;\n", 1),
    ("src/frontend/parser_statement.c", "    local.storage_offset = 0U;\n", 2),
):
    target = Path(frontend_path)
    text = target.read_text(encoding="utf-8")
    count = text.count(line)
    if count != expected_count:
        raise SystemExit(
            f"{frontend_path}: expected {expected_count} local placement mirror initializers, found {count}"
        )
    target.write_text(text.replace(line, ""), encoding="utf-8")

print("REMOVED frontend local placement mirror initializers=3")
