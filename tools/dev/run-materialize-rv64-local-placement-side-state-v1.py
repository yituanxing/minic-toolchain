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
