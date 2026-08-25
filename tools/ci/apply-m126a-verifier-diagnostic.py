#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_ir.c")
source = path.read_text()
start_marker = "bool minic_core_function_verify(const MinicCoreFunction *function) {"
end_marker = "bool minic_core_function_dump(FILE *output, const MinicCoreFunction *function) {"
begin = source.find(start_marker)
if begin < 0:
    raise SystemExit("Core verifier start marker not found")
end = source.find(end_marker, begin)
if end < 0:
    raise SystemExit("Core verifier dump marker not found")
body = source[begin:end]
needle = "return false;"
count = body.count(needle)
if count == 0:
    raise SystemExit("Core verifier has no false returns to instrument")
replacement = (
    'do { '
    'if (function != NULL && function->name != NULL && '
    'strcmp(function->name, "dump_kernel_instr") == 0) { '
    '(void)fprintf(stderr, "M126A_VERIFY_FAIL line=%d function=%s\\n", '
    '__LINE__, function->name); '
    '} '
    'return false; '
    '} while (0);'
)
body = body.replace(needle, replacement)
path.write_text(source[:begin] + body + source[end:])
print(f"M126A verifier diagnostic staged false_returns={count}")
