#!/usr/bin/env python3
"""Readonly temporary AST memory/shape diagnostic for pinned Linux .i inputs.

Never modify production C or CI compiler outputs. This instruments compiler.c
in a disposable Actions checkout. A/B compares output .s to the current
runtime-certified compiler, with same source SHA. No algorithm change.
"""
from pathlib import Path
p=Path("src/compiler/compiler.c")
s=p.read_text()
def once(old,new):
    global s
    if s.count(old)!=1:
        raise SystemExit("AST_SHAPE=ANCHOR_MISMATCH n=%d key=%r"%(s.count(old),old[:100]))
    s=s.replace(old,new,1)
helper=r'''
/* Diagnostic checkout only: no allocation or mutation of Program objects. */
static void minic_ast_shape_census(const MinicC0Program *program, const char *stage) {
    size_t i, calls = 0U, arguments = 0U, zero = 0U, one = 0U, two = 0U;
    size_t three_four = 0U, five_eight = 0U, nine_plus = 0U;
    size_t function_params = 0U;
    size_t function_0 = 0U, function_1 = 0U, function_2 = 0U;
    size_t function_3to4 = 0U, function_5to8 = 0U, function_9plus = 0U;
    size_t function_specialized = 0U, function_symbolic_used = 0U, function_boolean_used = 0U;
    if (getenv("MINIC_AST_SHAPE_CENSUS") == NULL || program == NULL) return;
    for (i=0U; i<program->expression_count; ++i) {
        const MinicExpression *e = &program->expressions[i];
        size_t n;
        if (e->kind != MINIC_EXPRESSION_CALL) continue;
        ++calls;
        n=e->value.call.argument_count;
        arguments += n;
        if (n==0U) ++zero; else if (n==1U) ++one; else if (n==2U) ++two;
        else if (n<=4U) ++three_four; else if (n<=8U) ++five_eight;
        else ++nine_plus;
    }
    for (i=0U; i<program->function_count; ++i) {
        const MinicFunction *function = &program->functions[i];
        size_t n=function->parameter_count, p;
        function_params += n;
        if (n==0U) ++function_0; else if (n==1U) ++function_1;
        else if (n==2U) ++function_2; else if (n<=4U) ++function_3to4;
        else if (n<=8U) ++function_5to8; else ++function_9plus;
        if (function->is_integer_specialization) ++function_specialized;
        for (p=0U; p<n && p<MINIC_MAX_FUNCTION_PARAMETERS; ++p) {
            if (function->specialization_symbolic_known[p]) ++function_symbolic_used;
            if (function->specialization_boolean_range_known[p]) ++function_boolean_used;
        }
    }
    (void)fprintf(stderr,
        "MINIC_AST_SHAPE stage=%s expression_size=%zu expression_count=%zu expression_capacity=%zu "
        "call_count=%zu call_arguments_total=%zu call_0=%zu call_1=%zu call_2=%zu "
        "call_3to4=%zu call_5to8=%zu call_9plus=%zu "
        "function_size=%zu function_count=%zu function_capacity=%zu function_params=%zu "
        "function_0=%zu function_1=%zu function_2=%zu function_3to4=%zu function_5to8=%zu "
        "function_9plus=%zu function_specialized=%zu function_symbolic_used=%zu "
        "function_boolean_used=%zu "
        "function_type_size=%zu function_type_count=%zu "
        "record_count=%zu local_count=%zu statement_count=%zu\n",
        stage, sizeof(MinicExpression),program->expression_count,program->expression_capacity,
        calls,arguments,zero,one,two,three_four,five_eight,nine_plus,
        sizeof(MinicFunction),program->function_count,program->function_capacity,
        function_params,function_0,function_1,function_2,function_3to4,
        function_5to8,function_9plus,function_specialized,function_symbolic_used,
        function_boolean_used,sizeof(MinicFunctionType),program->function_type_count,
        program->record_count,program->local_count,program->statement_count);
}
'''
once("int minic_compile_preprocessed_file_with_options(const char *input_path,",
     helper+"int minic_compile_preprocessed_file_with_options(const char *input_path,")
once('    minic_bootstrap_trace_stage(input_path, "parse", success ? "end-ok" : "end-fail",\n                                program.function_count);',
     '    minic_bootstrap_trace_stage(input_path, "parse", success ? "end-ok" : "end-fail",\n                                program.function_count);\n'
     '    if (success) minic_ast_shape_census(&program,"parsed");')
once("    minic_core_function_set_destroy(&core_set);\n    minic_c0_program_destroy(&program);",
     '    if (success) minic_ast_shape_census(&program,"final");\n'
     "    minic_core_function_set_destroy(&core_set);\n    minic_c0_program_destroy(&program);")
p.write_text(s)
print("MINIC_AST_SHAPE_SOURCE_INSTRUMENT=PASS status=read_only")
