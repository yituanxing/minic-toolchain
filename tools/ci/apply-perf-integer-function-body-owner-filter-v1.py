#!/usr/bin/env python3
"""PERF opt-in: use canonical source FunctionBody ownership in integer transitive.

Apply after, in order:
  1) apply-perf-symbolic-function-body-owner-filter-v0.py (supplies helper)
  2) apply-perf-integer-call-expression-index-v1.py
Retain original CALL expression ordering and candidate first-match behavior.
Absent/ambiguous ownership or allocation/validation failure falls back to
the exact prior expression scan.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()


def replace_in_block(before, after, label, from_offset=0):
    global text
    pos = text.find(before, from_offset)
    if pos < 0:
        raise SystemExit(f"integer owner {label}: missing composed Runtime anchor")
    text = text[:pos] + after + text[pos + len(before):]


# Enter integer-specialization routine; do not patch symbolic transitive.
func = text.find("static bool minic_specialize_inline_integer_calls(")
if func < 0:
    raise SystemExit("integer owner: integer specialization absent")
begin = text.find("size_t refinement_count = 0U;", func)
if begin < 0:
    raise SystemExit("integer owner: integer transitive absent")
finish = text.find("    if (specialization_count != 0U) {", begin)
if finish < 0:
    raise SystemExit("integer owner: integer phase end absent")

anchor = "        const size_t expression_limit = program->expression_count;\n"
inject = '''        const size_t expression_limit = program->expression_count;
        /* Optional canonical source-owner map. The count is stable for this
         * phase; clones add functions, not expression nodes. */
        const size_t integer_owner_count = program->expression_count;
        MinicFunctionId *integer_expression_owners = NULL;
        bool integer_owner_index_ready = false;
        if (integer_owner_count != 0U &&
            integer_owner_count <= SIZE_MAX / sizeof(*integer_expression_owners)) {
            integer_expression_owners = malloc(
                integer_owner_count * sizeof(*integer_expression_owners));
            if (integer_expression_owners != NULL) {
                integer_owner_index_ready =
                    minic_perf_program_build_expression_source_owners(
                        program, original_function_count,
                        integer_expression_owners, integer_owner_count);
                if (!integer_owner_index_ready) {
                    free(integer_expression_owners);
                    integer_expression_owners = NULL;
                }
            }
        }
'''
if text[begin:finish].count(anchor) != 1:
    raise SystemExit("integer owner: expected one CALL-index count anchor")
replace_in_block(anchor, inject, "map initialisation", begin)
finish = text.find("    if (specialization_count != 0U) {", begin)

nested = '''                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;'''
filtered = '''                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;
                if (integer_owner_index_ready &&
                    nested_expression_index < integer_owner_count) {
                    MinicFunctionId expression_owner =
                        integer_expression_owners[nested_expression_index];
                    if (expression_owner != MINIC_FUNCTION_INVALID &&
                        expression_owner < original_function_count &&
                        expression_owner != caller.specialization_source) {
                        continue;
                    }
                }'''
if text[begin:finish].count(nested) != 1:
    raise SystemExit("integer owner: unexpected transitive nested site count")
replace_in_block(nested, filtered, "owner mismatch", begin)
finish = text.find("    if (specialization_count != 0U) {", begin)

# The only error exit in this transitive block is failed clone allocation or
# reaching the clone budget. Ensure optional array is also released there.
# Find clone creation error exit inside the already scoped integer-transitive
# block. Indentation varied among Runtime patch generations; insist that the
# matched return is IMMEDIATELY after this specific clone call.
creation="&refined_id)) {"
area=text[begin:finish]
if area.count(creation)!=1:
    raise SystemExit(f"integer owner: clone error call count {area.count(creation)}")
call_pos=begin+area.index(creation)+len(creation)
return_pos=text.find("return false;", call_pos, finish)
if return_pos<0 or return_pos-call_pos>2048:
    raise SystemExit("integer owner: clone-allocation error return out of bounds")
# The verified original source can log an error before this return, so it is
# not necessarily whitespace-only; free immediately before the fail return.
text=text[:return_pos]+"free(integer_expression_owners);\n"+text[return_pos:]
finish = text.find("    if (specialization_count != 0U) {", begin)
cleanup='''        if (refinement_count != 0U) {'''
if text[begin:finish].count(cleanup) != 1:
    raise SystemExit("integer owner: transitive normal cleanup anchor mismatch")
replace_in_block(cleanup, '''        free(integer_expression_owners);
        if (refinement_count != 0U) {''', "normal cleanup", begin)

p.write_text(text)
print("MINIC_PERF_INTEGER_FUNCTION_BODY_OWNER_FILTER_V1=APPLIED")
