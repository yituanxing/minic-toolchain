#!/usr/bin/env python3
"""Independent P15 experiment: skip non-CALL ExpressionIds in symbolic transitive pass.

Apply after the canonical Linux profile, including the FunctionBody owner filter.
Original semantics scan an expression arena that may GROW during specialization.
We index only the initial arena; every later-appended ExpressionId is still
visited in order. Allocation failure falls back to the original linear walk.
No AST rewrites or specialization decisions are changed by this optimization.
"""
from pathlib import Path
p = Path("src/compiler/compiler.c")
source = p.read_text()

def once(old, new):
    global source
    n = source.count(old)
    if n != 1:
        raise SystemExit(f"P15 anchor count={n}: {old[:110]!r}")
    source = source.replace(old, new, 1)

once(
    """        (void)original_function_count;
        for (caller_index = 0U;
""",
    """        /* Snapshot only the original CALL ids. Expression closure may
         * append to the arena; dynamically scan the entire appended tail
         * to preserve the old loop's order and growth semantics. */
        size_t symbolic_initial_expression_count = program->expression_count;
        size_t *symbolic_call_work = NULL;
        size_t symbolic_call_count = 0U;
        bool symbolic_use_call_work = false;
        size_t symbolic_work_index;
        if (symbolic_initial_expression_count == 0U) {
            symbolic_use_call_work = true;
        } else if (symbolic_initial_expression_count <=
                   SIZE_MAX / sizeof(*symbolic_call_work)) {
            symbolic_call_work = (size_t *)malloc(
                symbolic_initial_expression_count * sizeof(*symbolic_call_work));
            symbolic_use_call_work = symbolic_call_work != NULL;
        }
        if (symbolic_use_call_work) {
            for (symbolic_work_index = 0U;
                 symbolic_work_index < symbolic_initial_expression_count;
                 ++symbolic_work_index) {
                if (program->expressions[symbolic_work_index].kind ==
                    MINIC_EXPRESSION_CALL) {
                    symbolic_call_work[symbolic_call_count++] = symbolic_work_index;
                }
            }
        }
        (void)original_function_count;
        for (caller_index = 0U;
"""
)
once(
    """            for (nested_expression_index = 0U;
                 nested_expression_index < program->expression_count;
                 ++nested_expression_index) {
""",
    """            for (symbolic_work_index = 0U;; ++symbolic_work_index) {
                if (!symbolic_use_call_work) {
                    /* OOM: the complete historical ExpressionId scan. */
                    if (symbolic_work_index >= program->expression_count) {
                        break;
                    }
                    nested_expression_index = symbolic_work_index;
                } else if (symbolic_work_index < symbolic_call_count) {
                    nested_expression_index =
                        symbolic_call_work[symbolic_work_index];
                } else {
                    /* Appended expressions are never skipped, including
                     * potential new CALL expressions in future variants. */
                    size_t appended_index =
                        symbolic_work_index - symbolic_call_count;
                    if (program->expression_count < symbolic_initial_expression_count ||
                        appended_index >=
                            program->expression_count - symbolic_initial_expression_count) {
                        break;
                    }
                    nested_expression_index =
                        symbolic_initial_expression_count + appended_index;
                }
"""
)
once(
    """                        free(symbolic_expression_owners);
                        return false;
""",
    """                        free(symbolic_call_work);
                        free(symbolic_expression_owners);
                        return false;
"""
)
once(
    """        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
""",
    """        free(symbolic_call_work);
        free(symbolic_expression_owners);
    }

    if (initial_clone_count != 0U || transitive_clone_count != 0U) {
"""
)
p.write_text(source)
print("MINIC_SYMBOLIC_CALL_WORKLIST_AUDIT_V1=APPLIED")
