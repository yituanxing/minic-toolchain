#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()

replacements = [
    (
        "!minic_specialize_inline_integer_calls(&program, target_info)",
        "!minic_perf_trace_specialize_integer(&program, target_info, input_path)",
    ),
    (
        "!minic_specialize_inline_symbolic_calls(&program, target_info)",
        "!minic_perf_trace_specialize_symbolic(&program, target_info, input_path)",
    ),
    (
        "!minic_specialize_inline_boolean_range_calls(&program, target_info)",
        "!minic_perf_trace_specialize_boolean(&program, target_info, input_path)",
    ),
    (
        "!minic_finalize_inline_integer_specialization_references(&program)",
        "!minic_perf_trace_finalize_specialization(&program, input_path)",
    ),
]
for old,new in replacements:
    count=text.count(old)
    if count < 1:
        raise SystemExit(f"missing specialization trace anchor: {old}")
    text=text.replace(old,new)

anchor="static bool minic_validate_core_functions(const char *input_path,\n"
if text.count(anchor) != 1:
    raise SystemExit(f"validate-core anchor count={text.count(anchor)}")

helpers=r'''
static bool minic_perf_trace_specialize_integer(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const char *input_path) {
    bool ok;
    minic_bootstrap_trace_stage(input_path, "spec-integer", "begin",
                                program != NULL ? program->function_count : 0U);
    ok = minic_specialize_inline_integer_calls(program, target);
    minic_bootstrap_trace_stage(input_path, "spec-integer", "end",
                                program != NULL ? program->function_count : 0U);
    return ok;
}

static bool minic_perf_trace_specialize_symbolic(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const char *input_path) {
    bool ok;
    minic_bootstrap_trace_stage(input_path, "spec-symbolic", "begin",
                                program != NULL ? program->function_count : 0U);
    ok = minic_specialize_inline_symbolic_calls(program, target);
    minic_bootstrap_trace_stage(input_path, "spec-symbolic", "end",
                                program != NULL ? program->function_count : 0U);
    return ok;
}

static bool minic_perf_trace_specialize_boolean(
    MinicC0Program *program,
    const MinicTargetInfo *target,
    const char *input_path) {
    bool ok;
    minic_bootstrap_trace_stage(input_path, "spec-boolean", "begin",
                                program != NULL ? program->function_count : 0U);
    ok = minic_specialize_inline_boolean_range_calls(program, target);
    minic_bootstrap_trace_stage(input_path, "spec-boolean", "end",
                                program != NULL ? program->function_count : 0U);
    return ok;
}

static bool minic_perf_trace_finalize_specialization(
    MinicC0Program *program,
    const char *input_path) {
    bool ok;
    minic_bootstrap_trace_stage(input_path, "spec-finalize", "begin",
                                program != NULL ? program->function_count : 0U);
    ok = minic_finalize_inline_integer_specialization_references(program);
    minic_bootstrap_trace_stage(input_path, "spec-finalize", "end",
                                program != NULL ? program->function_count : 0U);
    return ok;
}

'''
text=text.replace(anchor,helpers+anchor,1)
p.write_text(text)
print("MINIC_PERF_SPECIALIZATION_PHASE_TRACE_V0=APPLIED")
