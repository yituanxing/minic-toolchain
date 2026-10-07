#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

function_anchor = "static bool minic_specialize_inline_symbolic_calls(\n"
function_start = text.find(function_anchor)
if function_start < 0:
    raise SystemExit("symbolic specialization function not found")

next_function = text.find("\nstatic ", function_start + len(function_anchor))
if next_function < 0:
    raise SystemExit("symbolic specialization function end not found")

body = text[function_start:next_function]

old = '''    size_t transitive_clone_count = 0U;
    size_t original_function_count;
'''
new = '''    size_t transitive_clone_count = 0U;
    size_t original_function_count;
    size_t specialization_begin;
    size_t candidate_scan_index;
'''
if body.count(old) != 1:
    raise SystemExit(f"expected one symbolic local declaration block, found {body.count(old)}")
body = body.replace(old, new, 1)

old = '''    original_function_count = program->function_count;

    for (expression_index = 0U; expression_index < program->expression_count;
'''
new = '''    original_function_count = program->function_count;
    specialization_begin = original_function_count;
    for (candidate_scan_index = 0U;
         candidate_scan_index < original_function_count;
         ++candidate_scan_index) {
        if (program->functions[candidate_scan_index].is_integer_specialization) {
            specialization_begin = candidate_scan_index;
            break;
        }
    }

    for (expression_index = 0U; expression_index < program->expression_count;
'''
if body.count(old) != 1:
    raise SystemExit(f"expected one symbolic function-count anchor, found {body.count(old)}")
body = body.replace(old, new, 1)

old = "for (candidate_index = 0U; candidate_index < program->function_count;"
count = body.count(old)
if count != 2:
    raise SystemExit(f"expected two symbolic candidate full scans, found {count}")
body = body.replace(
    old,
    "for (candidate_index = specialization_begin; candidate_index < program->function_count;",
)

text = text[:function_start] + body + text[next_function:]
p.write_text(text)
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_CANDIDATE_RANGE_V0=APPLIED")
