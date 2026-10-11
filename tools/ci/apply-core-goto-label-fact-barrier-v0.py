#!/usr/bin/env python3
"""Invalidate straight-line local facts at a C label with possible goto reentry.

A label may be entered from a later goto, not only through lexical fallthrough.
For example Linux rpm_resume() initializes parent=NULL before repeat:, but may
assign parent=dev->parent and goto repeat later. Compile-time facts from the
first arrival cannot be assumed on that backedge.
"""
from pathlib import Path

path = Path("src/core/core_lower.c")
src = path.read_text()
old = """        } else if (statement->kind == MINIC_STATEMENT_LABEL) {
            const MinicStatement *loop = NULL;
"""
new = """        } else if (statement->kind == MINIC_STATEMENT_LABEL) {
            /* C labels are control-flow join points. A backward goto can
               invalidate all local facts learned on the first arrival.
               Keep the IR branches; only drop speculative local facts. */
            core_local_constants_clear_known(context);
            const MinicStatement *loop = NULL;
"""
if src.count(old) != 1:
    raise SystemExit(f"GOTO_LABEL_FACT_BARRIER=ERROR anchor_count={src.count(old)}")
path.write_text(src.replace(old, new, 1))
print("GOTO_LABEL_FACT_BARRIER=APPLIED label_entry_facts_cleared=true")
