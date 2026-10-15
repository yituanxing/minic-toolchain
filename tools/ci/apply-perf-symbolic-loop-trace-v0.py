#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

direct_anchor = '''    original_function_count = program->function_count;

    for (expression_index = 0U; expression_index < program->expression_count;
'''
direct_repl = '''    original_function_count = program->function_count;
    minic_bootstrap_trace_stage(NULL, "spec-symbolic-direct", "begin",
                                program->function_count);

    for (expression_index = 0U; expression_index < program->expression_count;
'''
if text.count(direct_anchor) != 1:
    raise SystemExit(f"symbolic direct anchor count={text.count(direct_anchor)}")
text = text.replace(direct_anchor, direct_repl, 1)

transitive_anchor = '''        program->expressions[expression_index].value.call.function_id = variant_id;
    }

    {
        size_t caller_index;
'''
transitive_repl = '''        program->expressions[expression_index].value.call.function_id = variant_id;
    }
    minic_bootstrap_trace_stage(NULL, "spec-symbolic-direct", "end-success",
                                program->function_count);
    minic_bootstrap_trace_stage(NULL, "spec-symbolic-transitive", "begin",
                                program->function_count);

    {
        size_t caller_index;
'''
if text.count(transitive_anchor) != 1:
    raise SystemExit(f"symbolic transitive start anchor count={text.count(transitive_anchor)}")
text = text.replace(transitive_anchor, transitive_repl, 1)

end_anchor = '''    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
'''
end_repl = '''    minic_bootstrap_trace_stage(NULL, "spec-symbolic-transitive", "end-success",
                                program->function_count);
    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
'''
if text.count(end_anchor) != 1:
    raise SystemExit(f"symbolic transitive end anchor count={text.count(end_anchor)}")
text = text.replace(end_anchor, end_repl, 1)

p.write_text(text)
print("MINIC_PERF_SYMBOLIC_LOOP_TRACE_V0=APPLIED")
