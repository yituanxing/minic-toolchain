#!/usr/bin/env python3
from pathlib import Path

path = Path("src/target/riscv64/codegen_expression.c")
source = path.read_text()

start_marker = """        {\n            MinicRiscv64AbiCursor abi_cursor;\n\n            minic_riscv64_abi_cursor_initialize(&abi_cursor);\n"""
end_marker = """\n        if (is_indirect) {\n"""
start = source.find(start_marker)
if start < 0:
    raise SystemExit("caller ABI trace start marker missing")
end = source.find(end_marker, start)
if end < 0:
    raise SystemExit("caller ABI trace end marker missing")
region = source[start:end]

needle = "return false;"
site = 0
cursor = 0
pieces = []
while True:
    index = region.find(needle, cursor)
    if index < 0:
        pieces.append(region[cursor:])
        break
    site += 1
    pieces.append(region[cursor:index])
    pieces.append(
        'fprintf(stderr, "CALLER_ABI_LOC_FAIL site=%d arg=%%zu\\n", argument_index); '
        'return false;' % site
    )
    cursor = index + len(needle)

if site == 0:
    raise SystemExit("caller ABI trace found no failure sites")
source = source[:start] + "".join(pieces) + source[end:]
path.write_text(source)
print(f"MATERIALIZED rv64-caller-abi-location-trace sites={site}")
