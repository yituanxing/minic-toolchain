#!/usr/bin/env python3
"""Opt-in CALL-ID acceleration for symbolic transitive specialization.

Apply AFTER apply-perf-symbolic-function-body-owner-filter-v0.py on the
composed Runtime profile. Preserve ExpressionId traversal order. If memory
allocation fails or expressions are appended, use the exact original scan.
No change to function ownership semantics, clones, or reachability.
"""
from pathlib import Path

p = Path("src/compiler/compiler.c")
src = p.read_text()


def once(old, new, label, after=0):
    global src
    ix = src.find(old, after)
    if ix < 0 or src.find(old, ix + len(old)) >= 0:
        raise SystemExit(f"{label}: expected exactly one composed Runtime anchor")
    src = src[:ix] + new + src[ix + len(old):]


anchor = '''        if (!symbolic_owner_index_ready) {
            free(symbolic_expression_owners);
            symbolic_expression_owners = NULL;
        }
        (void)original_function_count;'''
replacement = '''        if (!symbolic_owner_index_ready) {
            free(symbolic_expression_owners);
            symbolic_expression_owners = NULL;
        }
        /* CALL-only index, stable ascending expression ID; optional. */
        size_t *symbolic_call_ids = NULL;
        size_t symbolic_call_count = 0U;
        size_t symbolic_call_scan;
        if (symbolic_owned_expression_count != 0U &&
            symbolic_owned_expression_count <= SIZE_MAX / sizeof(*symbolic_call_ids)) {
            symbolic_call_ids = malloc(
                symbolic_owned_expression_count * sizeof(*symbolic_call_ids));
            if (symbolic_call_ids != NULL) {
                for (symbolic_call_scan = 0U;
                     symbolic_call_scan < symbolic_owned_expression_count;
                     ++symbolic_call_scan) {
                    if (program->expressions[symbolic_call_scan].kind ==
                        MINIC_EXPRESSION_CALL) {
                        symbolic_call_ids[symbolic_call_count++] = symbolic_call_scan;
                    }
                }
            }
        }
        (void)original_function_count;'''
once(anchor, replacement, "index initialization")
anchor_loop = '''            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];'''
replacement_loop = '''            for (nested_call_pos = 0U;
                 nested_call_pos <
                     (symbolic_call_ids != NULL &&
                      program->expression_count == symbolic_owned_expression_count
                          ? symbolic_call_count : program->expression_count);
                 ++nested_call_pos) {
                size_t nested_expression_index =
                    symbolic_call_ids != NULL &&
                    program->expression_count == symbolic_owned_expression_count
                        ? symbolic_call_ids[nested_call_pos] : nested_call_pos;
                const MinicExpression *nested =
                    &program->expressions[nested_expression_index];'''
# Only target symbolic clone work, never the integer transitive algorithm.
pos = src.find("size_t symbolic_owned_expression_count")
if pos < 0:
    raise SystemExit("symbolic owner patch absent")
start = src.find("size_t nested_expression_index;", pos)
if start < 0:
    raise SystemExit("symbolic nested cursor missing")
src = src[:start] + src[start:].replace(
    "size_t nested_expression_index;", "size_t nested_call_pos;", 1)
pos = src.find("size_t symbolic_owned_expression_count")
ix = src.find(anchor_loop, pos)
if ix < 0 or src.find(anchor_loop, ix + len(anchor_loop)) >= 0:
    raise SystemExit("symbolic transitive scan anchor count != 1")
src = src[:ix] + replacement_loop + src[ix + len(anchor_loop):]
# Existing owner patch owns every transitive early-exit; extend its cleanup.
pos = src.find("size_t symbolic_owned_expression_count")
fail = '''                        free(symbolic_expression_owners);
                        return false;'''
idx = src.find(fail, pos)
if idx < 0:
    raise SystemExit("symbolic failure cleanup missing")
src = src[:idx] + src[idx:].replace(
    fail, '''                        free(symbolic_call_ids);
                        free(symbolic_expression_owners);
                        return false;''', 1)
end = '''        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {'''
if src.count(end) != 1:
    raise SystemExit("symbolic normal cleanup anchor absent")
src = src.replace(
    end,
    '''        free(symbolic_call_ids);
        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {''', 1)
p.write_text(src)
print("MINIC_PERF_SYMBOLIC_CALL_INDEX_V1=APPLIED")
