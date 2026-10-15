#!/usr/bin/env python3
from pathlib import Path
p=Path("src/compiler/compiler.c")
s=p.read_text()
a="        size_t refinement_count = 0U;\n\n        for (caller_index = original_function_count;"
z="        if (refinement_count != 0U) {"
i=s.find(a);j=s.find(z,i)
if i<0 or j<i: raise SystemExit("transitive integer block absent")
q=s[i:j]
def change(old,new):
    global q
    if q.count(old)!=1: raise SystemExit("anchor mismatch "+old[:70])
    q=q.replace(old,new,1)
change(a,'''        size_t refinement_count = 0U;
        size_t *calls = NULL, call_count = 0U, pos;
        const size_t expression_limit = program->expression_count;
        if (expression_limit > 0U &&
            expression_limit <= SIZE_MAX / sizeof(*calls)) {
            calls = malloc(expression_limit * sizeof(*calls));
            if (calls != NULL) {
                for (pos = 0U; pos < expression_limit; ++pos)
                    if (program->expressions[pos].kind == MINIC_EXPRESSION_CALL)
                        calls[call_count++] = pos;
            }
        }

        for (caller_index = original_function_count;''')
change("            size_t nested_expression_index;","            size_t nested_scan_index;")
change('''            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested = &program->expressions[nested_expression_index];''','''            for (nested_scan_index = 0U;
                 nested_scan_index < ((calls != NULL &&
                    program->expression_count == expression_limit)
                        ? call_count : program->expression_count);
                 ++nested_scan_index) {
                size_t nested_expression_index =
                    (calls != NULL && program->expression_count == expression_limit)
                    ? calls[nested_scan_index] : nested_scan_index;
                const MinicExpression *nested = &program->expressions[nested_expression_index];''')
change("                        return false;\n                    }\n                    specialization_count += 1U;","                        free(calls);\n                        return false;\n                    }\n                    specialization_count += 1U;")
s=s[:i]+q+"        free(calls);\n"+s[j:]
p.write_text(s)
print("MINIC_PERF_INTEGER_CALL_EXPR_INDEX_V1=APPLIED")
