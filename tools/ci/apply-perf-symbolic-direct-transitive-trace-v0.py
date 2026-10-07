#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
start = text.find("static bool minic_specialize_inline_symbolic_calls(\n")
end = text.find("    if (initial_clone_count != 0U || transitive_clone_count != 0U) {", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate symbolic specialization pass")
body = text[start:end]

direct = "    for (expression_index = 0U; expression_index < program->expression_count;\n"
if body.count(direct) != 1:
    raise SystemExit(f"symbolic direct loop anchor={body.count(direct)}")
body = body.replace(
    direct,
    '    minic_bootstrap_trace_stage(NULL, "spec-symbolic-direct", "begin",\n'
    '                                program->function_count);\n'
    + direct,
    1,
)

transitive = "    {\n        size_t caller_index;\n"
if body.count(transitive) != 1:
    raise SystemExit(f"symbolic transitive block anchor={body.count(transitive)}")
body = body.replace(
    transitive,
    '    minic_bootstrap_trace_stage(NULL, "spec-symbolic-direct", "end",\n'
    '                                program->function_count);\n'
    '    minic_bootstrap_trace_stage(NULL, "spec-symbolic-transitive", "begin",\n'
    '                                program->function_count);\n'
    + transitive,
    1,
)

body += '    minic_bootstrap_trace_stage(NULL, "spec-symbolic-transitive", "end",\n' \
        '                                program->function_count);\n'
text = text[:start] + body + text[end:]
p.write_text(text)
print("MINIC_PERF_SYMBOLIC_DIRECT_TRANSITIVE_TRACE_V0=APPLIED")
