#!/usr/bin/env python3
from pathlib import Path

path = Path("tests/core/run-core-ir-shadow.sh")
text = path.read_text()
old = """cat >\"$work_dir/unsupported.i\" <<'EOF'\nint runtime_subtract(int value) {\n    return value - 2;\n}\nEOF\n\n\"$MINIC\" -S \"$work_dir/unsupported.i\" -o \"$work_dir/unsupported-normal.s\"\nMINIC_CORE_IR=shadow \"$MINIC\" -S \"$work_dir/unsupported.i\" -o \"$work_dir/unsupported-shadow.s\"\ncmp \"$work_dir/unsupported-normal.s\" \"$work_dir/unsupported-shadow.s\"\n\nif MINIC_CORE_IR=strict \"$MINIC\" -S \"$work_dir/unsupported.i\" \\\n    -o \"$work_dir/unsupported-strict.s\" 2>\"$work_dir/unsupported-strict.err\"; then\n    echo \"strict Core IR shadow unexpectedly accepted an unsupported function\" >&2\n    exit 1\nfi\ngrep -F \"Core IR shadow does not yet support function 'runtime_subtract'\" \\\n    \"$work_dir/unsupported-strict.err\" >/dev/null\n\n"""
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one stale runtime_subtract negative block, found {count}")
path.write_text(text.replace(old, "", 1))
