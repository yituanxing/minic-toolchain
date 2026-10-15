#!/usr/bin/env python3
"""Diagnostic-only integer inline specialization direct/transitive timers.

Apply AFTER the regular Runtime profile (with transitive specialization)
and the outer four-stage trace. Never modify compilation decisions.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()


def replace_one(old, new, label):
    global text
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"integer phase {label}: expected exactly 1 anchor, got {n}")
    text = text.replace(old, new, 1)


direct='''    original_function_count = program->function_count;
    for (expression_index = 0U; expression_index < program->expression_count;
'''
replace_one(direct, '''    original_function_count = program->function_count;
    minic_bootstrap_trace_stage(NULL, "spec-integer-direct", "begin",
                                program->function_count);
    for (expression_index = 0U; expression_index < program->expression_count;
''', "begin")

# End direct after all expression->function_id updates; transitive immediately
# follows in the currently tested Runtime profile.
start='''        program->expressions[expression_index].value.call.function_id = specialized_id;
    }

    {
        size_t caller_index;
'''
replace_one(start, '''        program->expressions[expression_index].value.call.function_id = specialized_id;
    }
    minic_bootstrap_trace_stage(NULL, "spec-integer-direct", "end-success",
                                program->function_count);
    minic_bootstrap_trace_stage(NULL, "spec-integer-transitive", "begin",
                                program->function_count);
    {
        size_t caller_index;
''', "transition")

finish='''    if (specialization_count != 0U) {
'''
replace_one(finish, '''    minic_bootstrap_trace_stage(NULL, "spec-integer-transitive", "end-success",
                                program->function_count);
    if (specialization_count != 0U) {
''', "end")

p.write_text(text)
print("MINIC_PERF_INTEGER_LOOP_TRACE_V0=APPLIED")
